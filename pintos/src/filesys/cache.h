
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
    struct list_elem elem_cache;

};

void cache_init(void);
struct buffer_cache* cache_exist(disk_sector_t sector);
