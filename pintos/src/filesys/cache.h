
#include <list.h>

#include <stdint.h>
#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

#define MAX_CACHE_SIZE 64

struct list buffer_cache_list;
struct lock buffer_cache_lock;

struct buffer_cache{
    disk_sector_t sector;
    uint8_t data[DISK_SECTOR_SIZE];
    bool is_used;
    bool is_dirty;
    bool is_using;
    struct list_elem elem;
};

void cache_init(void);
struct buffer_cache* find_cache(disk_sector_t sector);
void cache_read(disk_sector_t sector_idx, uint8_t* buffer, off_t bytes_read, int sector_ofs, int chunk_size);
void cache_write(disk_sector_t sector_idx, uint8_t* buffer, off_t bytes_read, int sector_ofs, int chunk_size);
struct buffer_cache* evict_cache(disk_sector_t sector_idx);
struct buffer_cache* allocate_new_cache(disk_sector_t sector_idx);
void cache_write_behind(void* aux);
void cache_write_behind_loop(void);