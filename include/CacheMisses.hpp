#pragma once
#include <papi.h>
#include <iostream>
#include <string>
#include <cassert>
#include <cstring>

struct CacheMissCounters {
    int EventSet = PAPI_NULL;
    long long values[1] = {0};
    bool initialized = false;
    
    CacheMissCounters() {
        // Initialize PAPI library
        // Note: In threaded apps, PAPI_library_init should ideally be called 
        // once by the main thread, but we check here just in case.
        if (!PAPI_is_initialized()) {
            int retval = PAPI_library_init(PAPI_VER_CURRENT);
            if(retval != PAPI_VER_CURRENT) {
                std::cerr << "PAPI library init error!" << std::endl;
                initialized = false;
                return;
            }
        }

        // Create an event set
        EventSet = PAPI_NULL;
        if(PAPI_create_eventset(&EventSet) != PAPI_OK) {
            std::cerr << "PAPI create event set error!" << std::endl;
            initialized = false;
            return;
        }

        // Try native events for L1 cache misses
        int event_code;
        const char* native_events[] = {
            "perf::L1-DCACHE-LOAD-MISSES",
            "perf::PERF_COUNT_HW_CACHE_L1D:MISS",
            "L1-dcache-load-misses",
            "L1-dcache-loads-misses",
            "PAPI_L1_DCM" // Standard PAPI preset as fallback
        };

        for (const char* event_name : native_events){
            // Check if event exists
            if (PAPI_event_name_to_code(const_cast<char*>(event_name), &event_code) == PAPI_OK) {
                // Try adding it to the set
                if (PAPI_add_event(EventSet, event_code) == PAPI_OK) {
                    initialized = true;
                    return;
                }
            }
        }

        // If native events not found, try using perf directly via component
        // Note: This is specific to certain architectures
        char raw_event_name[PAPI_MAX_STR_LEN];
        strcpy(raw_event_name, "perf_raw::r0151"); // Raw event code for L1 DCache Load Misses

        if(PAPI_event_name_to_code(raw_event_name, &event_code) == PAPI_OK) {
            if (PAPI_add_event(EventSet, event_code) == PAPI_OK) {
                initialized = true;
                return;
            }
        }
        
        std::cerr << "Failed to add L1 cache miss event to PAPI event set!" << std::endl;
        initialized = false;
    }
    
    void start_l1_cache_miss_counting() {
        if (initialized) {
            if (PAPI_start(EventSet) != PAPI_OK) {
                std::cerr << "PAPI start counting error!" << std::endl;
            }
        }
    }

    void stop_l1_cache_miss_counting() {
        if (initialized) {
            // Stops the counters and stores the result in values[0]
            if (PAPI_stop(EventSet, values) != PAPI_OK) {
                std::cerr << "PAPI stop counting error!" << std::endl;
            }
        }
    }

    // Helper to get raw count
    long long get_misses() const {
        return values[0];
    }

    double get_l1_cache_miss_rate(long long total_accesses) {
        if (initialized && total_accesses > 0) {
            return static_cast<double>(values[0]) / static_cast<double>(total_accesses);
        }
        return 0.0;
    }

    ~CacheMissCounters() {
        if (EventSet != PAPI_NULL) {
            PAPI_cleanup_eventset(EventSet);
            PAPI_destroy_eventset(&EventSet);
        }
        // Do not call PAPI_shutdown() here if other threads are still running PAPI
    }
};