
#include "devices/timer.h"
#include "threads/synch.h"
#include <list.h>
#include "filesys/off_t.h"
#include "filesys/filesys.h"
#include "devices/disk.h"
#include "threads/thread.h"
#include "filesys/cache.h"

#define TIMER_PERIOD 150

int cache_current_size;

void cache_init(void){
    list_init(&buffer_cache_list);
    lock_init(&buffer_cache_lock);
    cache_current_size = 0;
    thread_create("write_behind", PRI_MAX, cache_write_behind, 0);
}

struct buffer_cache* find_cache(disk_sector_t sector){
    
    struct buffer_cache* cache_e;
    struct list_elem* e;
    if(!list_empty(&buffer_cache_list)){
        for(e=list_begin(&buffer_cache_list); e!=list_end(&buffer_cache_list); e = list_next(e)){
            cache_e = list_entry(e, struct buffer_cache, elem);
            if(cache_e->sector == sector) return cache_e;
        }
    }
    return NULL;
}

struct buffer_cache* evict_cache(disk_sector_t sector_idx){
    /* use same method as evict_frame, second-chance algorithm */

    struct list_elem* e;
    struct buffer_cache* cache_e;
    if(!list_empty(&buffer_cache_list)){
        for(e=list_begin(&buffer_cache_list); e!=list_end(&buffer_cache_list); e=list_next(e)){
            cache_e = list_entry(e, struct buffer_cache, elem);
            if(cache_e->is_using) continue;
            if(cache_e->is_used) cache_e->is_used=false;
            else{ /* selected */
                if(cache_e->is_dirty) /*write back(write behind) */
                    disk_write(filesys_disk, cache_e->sector, &cache_e->data);
                // list_remove(&cache_e->elem); **do not need to remove because we reuse this
                cache_e->is_using = true;
                cache_e->sector = sector_idx;
                disk_read(filesys_disk, cache_e->sector, &cache_e->data);
                cache_e->is_dirty = false;

                return cache_e;
            }
        }
        for(e=list_begin(&buffer_cache_list); e!=list_end(&buffer_cache_list); e=list_next(e)){
            cache_e = list_entry(e, struct buffer_cache, elem);
            if(cache_e->is_using) continue;
            if(cache_e->is_used) cache_e->is_used=false;
            else{ /* selected */
                if(cache_e->is_dirty) /*write back(write behind) */
                    disk_write(filesys_disk, cache_e->sector, &cache_e->data);
                // list_remove(&cache_e->elem); **do not need to remove because we reuse this
                cache_e->is_using = true;
                cache_e->sector = sector_idx;
                disk_read(filesys_disk, cache_e->sector, &cache_e->data);
                cache_e->is_dirty = false;

                return cache_e;
            }
        }
    }

    return NULL;
}

struct buffer_cache* allocate_new_cache(disk_sector_t sector_idx){

    struct buffer_cache* new_cache_e = malloc(sizeof(struct buffer_cache));
    if(new_cache_e == NULL) ASSERT(0);
    list_push_back(&buffer_cache_list, &new_cache_e->elem);
    cache_current_size += 1;
    new_cache_e->sector = sector_idx;
    new_cache_e->is_used = true;
    new_cache_e->is_dirty = false;

    new_cache_e->is_using = true;
    disk_read(filesys_disk, sector_idx, new_cache_e->data);
    new_cache_e->is_using = false;

    return new_cache_e;
}

void cache_read(disk_sector_t sector_idx, uint8_t* buffer, off_t bytes_read, int sector_ofs, int chunk_size){
    lock_acquire(&buffer_cache_lock);
    struct buffer_cache* cache_e = find_cache(sector_idx);
    if(cache_e == NULL){
        if(cache_current_size < MAX_CACHE_SIZE) cache_e = allocate_new_cache(sector_idx);
        else cache_e = evict_cache(sector_idx);
        ASSERT(cache_e != NULL);
        
        cache_e->is_using = true; /* need eviction */
        lock_release(&buffer_cache_lock);
        memcpy(buffer+bytes_read, (uint8_t* )&cache_e->data + sector_ofs, chunk_size);
        cache_e->is_using = false;
    }
    else{
        cache_e->is_used = true;
        cache_e->is_using = true; /* need eviction */
        lock_release(&buffer_cache_lock);
        memcpy(buffer+bytes_read, (uint8_t* )&cache_e->data + sector_ofs, chunk_size);
        cache_e->is_using = false;
    }

    return;
}

void cache_write(disk_sector_t sector_idx, uint8_t* buffer, off_t bytes_read, int sector_ofs, int chunk_size){
    lock_acquire(&buffer_cache_lock);
    struct buffer_cache* cache_e = find_cache(sector_idx);
    if(cache_e == NULL){
        if (cache_current_size < MAX_CACHE_SIZE) cache_e = allocate_new_cache(sector_idx);
        else cache_e = evict_cache(sector_idx); /* need eviction */
        ASSERT(cache_e != NULL);

        cache_e->is_using = true;
        lock_release(&buffer_cache_lock);
        memcpy((uint8_t* )&cache_e->data + sector_ofs, buffer+bytes_read, chunk_size);
        cache_e->is_using = false;
        cache_e->is_dirty = true;
    }
    else{
        cache_e->is_used = true;
        cache_e->is_using = true;
        lock_release(&buffer_cache_lock);
        memcpy((uint8_t* )&cache_e->data + sector_ofs, buffer+bytes_read, chunk_size);
        cache_e->is_using = false;
        cache_e->is_dirty = true;
    }

    return;
}

void cache_write_behind_loop(void){
    struct list_elem* e;
    struct buffer_cache* cache_e;

    if(!list_empty(&buffer_cache_list)){
        for(e=list_begin(&buffer_cache_list); e!=list_end(&buffer_cache_list); e=list_next(e)){
            cache_e = list_entry(e, struct buffer_cache, elem);
            if(cache_e->is_dirty){
                cache_e->is_using = true;
                disk_write(filesys_disk, cache_e->sector, &cache_e->data);
                cache_e->is_using = false;
                cache_e->is_dirty = false;
            }
        }
    }
    return;
}

void cache_write_behind(void* aux){
    while(1){
        lock_acquire(&buffer_cache_lock);
        cache_write_behind_loop();
        lock_release(&buffer_cache_lock);
        timer_sleep(TIMER_PERIOD);
    }
    return;
}
