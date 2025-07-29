	  if (next.ptr == nullptr) continue;
	  value = next.ptr->value;
	  MSPointer<T> new_head(next.ptr, cur_head.count + 1);
	  if (head.compare_exchange_weak(cur_head, new_head)) break;
	}
      }
    }
    delete cur_head.ptr;
    return true;
  }
  
  
};
