#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "FairnessLogger.hpp"

// LCRQ: Low-Contention Ring Queue
// Based on "Fast Concurrent Queues for x86 Processors" by Morrison & Afek

template<typename T>
class LCRQ {   
private:
    std::mutex record_mutex;
    static constexpr int RING_SIZE = 1024;  // Must be power of 2
    
    // Entry states encoded in low bits of pointer
    static constexpr uintptr_t EMPTY = 0;
    static constexpr uintptr_t BUSY = 1;
    static constexpr uintptr_t VALID = 2;
    static constexpr uintptr_t STATE_MASK = 3;
    static constexpr uintptr_t PTR_MASK = ~STATE_MASK;
    
    // Special values for ring closure
    static constexpr int64_t CLOSED = -1;
    static constexpr int64_t UNSAFE_BIT = (1LL << 63);
    
    // Wrapper for items with timestamps
    struct TimestampedItem {
        T value;

        uint64_t enq_inv_ts;
        uint64_t enq_lin_ts;
        uint64_t deq_inv_ts;
        uint64_t deq_lin_ts;
        
        TimestampedItem(T&& val, uint64_t enq_inv_ts)
            : value(std::move(val)), enq_inv_ts(enq_inv_ts), enq_lin_ts(0), deq_inv_ts(0), deq_lin_ts(0) {}
    };

    // Single entry in the ring buffer
    struct Entry {
        std::atomic<uintptr_t> val;  // Pointer to TimestampedItem + state bits
        std::atomic<int64_t> idx;     // Logical index (for ABA prevention)
        
        Entry() : val(EMPTY), idx(0) {}
    };
    
    // A single ring buffer (CRQ - Concurrent Ring Queue)
    struct RingNode {
        Entry entries[RING_SIZE];
        std::atomic<int64_t> head;
        std::atomic<int64_t> tail;
        std::atomic<RingNode*> next;
        
        RingNode() : head(0), tail(0), next(nullptr) {
            for (int i = 0; i < RING_SIZE; i++) {
                entries[i].val.store(EMPTY, std::memory_order_relaxed);
                entries[i].idx.store(i, std::memory_order_relaxed);
            }
        }
        
        ~RingNode() {
            // Clean up any remaining valid entries
            for (int i = 0; i < RING_SIZE; i++) {
                uintptr_t val = entries[i].val.load(std::memory_order_relaxed);
                if ((val & STATE_MASK) == VALID) {
                    delete reinterpret_cast<TimestampedItem*>(val & PTR_MASK);
                }
            }
        }
    };
    
    std::atomic<RingNode*> head_ring;  // For dequeue
    std::atomic<RingNode*> tail_ring;  // For enqueue
    
    // Helper: Create entry value with state
    static uintptr_t make_entry(TimestampedItem* ptr, uintptr_t state) {
        return reinterpret_cast<uintptr_t>(ptr) | state;
    }
    
    // Helper: Extract pointer from entry
    static TimestampedItem* get_ptr(uintptr_t val) {
        return reinterpret_cast<TimestampedItem*>(val & PTR_MASK);
    }
    
    // Helper: Extract state from entry
    static uintptr_t get_state(uintptr_t val) {
        return val & STATE_MASK;
    }
    
    // Try to enqueue on a specific ring
    // Returns: 1 = success, 0 = ring full (closed), -1 = retry
    int ring_enqueue(RingNode* ring, TimestampedItem* item) {
        while (true) {
            // FAA to claim a slot
            int64_t tail = ring->tail.fetch_add(1, std::memory_order_relaxed);
            
            // Check if ring is closed
            if (tail >= RING_SIZE) {
                return 0;  // Ring full
            }
            
            Entry& entry = ring->entries[tail & (RING_SIZE - 1)];
            
            // Wait for the slot to be ready (idx must match)
            int64_t expected_idx = tail;
            while (true) {
                int64_t idx = entry.idx.load(std::memory_order_acquire);
                
                if (idx == expected_idx) {
                    // Slot is ready, try to store our item
                    uintptr_t expected = EMPTY;
                    
                    if (entry.val.compare_exchange_strong(
                            expected,
                            make_entry(item, VALID),
                            std::memory_order_release,
                            std::memory_order_relaxed)) {
                        
                        item->enq_lin_ts = now();
                        return 1;  // Successful enqueue
                    }
                    // CAS failed, someone else got it
                    return -1;  
                }
                
                if (idx & UNSAFE_BIT) {
                    // Ring closed while we were waiting
                    return 0;
                }
                
                // Check if we should give up on this slot
                uintptr_t val = entry.val.load(std::memory_order_relaxed);
                if (get_state(val) != EMPTY) {
                    return -1;  // Slot taken, retry
                }
            }
        }
    }
    
    // Try to dequeue from a specific ring
    // Returns: pointer to item if success, nullptr if empty/closed
    TimestampedItem* ring_dequeue(RingNode* ring) {
        while (true) {
            // FAA to claim a slot
            int64_t head = ring->head.fetch_add(1, std::memory_order_relaxed);
            
            // Check if we're past the ring
            if (head >= RING_SIZE) {
                return nullptr;  // Ring exhausted
            }
            
            Entry& entry = ring->entries[head & (RING_SIZE - 1)];
            
            // Wait for item to be available
            int attempts = 0;
            while (true) {
                uintptr_t val = entry.val.load(std::memory_order_acquire);
                
                if (get_state(val) == VALID) {
                    // Try to take the item
                    if (entry.val.compare_exchange_strong(
                            val,
                            EMPTY,
                            std::memory_order_release,
                            std::memory_order_relaxed)) {

                        // Update idx to allow reuse
                        TimestampedItem* ptr = get_ptr(val);
                        ptr->deq_lin_ts = now();
                        entry.idx.store(head + RING_SIZE, std::memory_order_release);
                        return ptr;
                    }
                    continue;  // CAS failed, retry
                }
                
                // Check if slot is empty but not yet filled
                int64_t idx = entry.idx.load(std::memory_order_acquire);
                if (idx == head) {
                    // Slot claimed but not filled yet
                    if (++attempts < 100) {
                        continue;  // Spin a bit
                    }
                }
                
                // Give up on this slot
                return nullptr;
            }
        }
    }
    
    // Close a ring when it's full
    void close_ring(RingNode* ring) {
        // Mark all unfilled slots as unsafe
        for (int i = 0; i < RING_SIZE; i++) {
            Entry& entry = ring->entries[i];
            int64_t idx = entry.idx.load(std::memory_order_relaxed);
            if (idx < RING_SIZE) {
                entry.idx.store(idx | UNSAFE_BIT, std::memory_order_relaxed);
            }
        }
    }

public:
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> records;

    LCRQ() {
        RingNode* initial = new RingNode();
        head_ring.store(initial, std::memory_order_relaxed);
        tail_ring.store(initial, std::memory_order_relaxed);
    }
    
    ~LCRQ() {
        RingNode* curr = head_ring.load(std::memory_order_relaxed);
        while (curr) {
            RingNode* next = curr->next.load(std::memory_order_relaxed);
            delete curr;
            curr = next;
        }
    }
    
    void enqueue(T item, int tid) {
        uint64_t enq_inv_ts = now();
        
        TimestampedItem* ptr = new TimestampedItem(std::move(item), enq_inv_ts);
        
        while (true) {
            RingNode* ring = tail_ring.load(std::memory_order_acquire);
            int result = ring_enqueue(ring, ptr);
            
            if (result == 1) {
                return; 
            }
            
            if (result == 0) {
                // Ring is full, need to allocate new one
                RingNode* next = ring->next.load(std::memory_order_acquire);
                
                if (next == nullptr) {
                    // Try to create and link new ring
                    RingNode* new_ring = new RingNode();
                    RingNode* expected = nullptr;
                    
                    if (ring->next.compare_exchange_strong(
                            expected,
                            new_ring,
                            std::memory_order_release,
                            std::memory_order_relaxed)) {
                        close_ring(ring);
                        tail_ring.store(new_ring, std::memory_order_release);
                    } else {
                        // Someone else created it
                        delete new_ring;
                        tail_ring.store(expected, std::memory_order_release);
                    }
                } else {
                    // Ring already exists
                    tail_ring.store(next, std::memory_order_release);
                }
            }
        }
    }
    
    bool dequeue(T* item, int tid) {
        uint64_t deq_inv_ts = now();

        while (true) {
            RingNode* ring = head_ring.load(std::memory_order_acquire);
            auto* item_ptr = ring_dequeue(ring);

            if (item_ptr) {
                std::lock_guard<std::mutex> lock(record_mutex);
                records.emplace_back(item_ptr->enq_inv_ts, item_ptr->enq_lin_ts, deq_inv_ts, item_ptr->deq_lin_ts);
                

                // Move value into caller-provided pointer
                *item = std::move(item_ptr->value);
                delete item_ptr;
                return true;
            }

            // Ring exhausted, try next one
            RingNode* next = ring->next.load(std::memory_order_acquire);
            if (next == nullptr) {
                RingNode* tail = tail_ring.load(std::memory_order_acquire);
                if (tail == ring) {
                    return false;  // Queue empty
                }
                continue; // Wait for next ring
            }

            // Advance to next ring
            head_ring.store(next, std::memory_order_release);
            // (Safe reclamation omitted)
        }
    }

};