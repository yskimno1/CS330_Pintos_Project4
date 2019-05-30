
#include <string.h>

#include "filesys/cache.h"
#include "threads/synch.h"

void cache_init(void){
    list_init(&buffer_cache_list);
    lock_init(&buffer_cache_lock);
    
}

