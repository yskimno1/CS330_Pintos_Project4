#include "vm/page.h"
#include "vm/frame.h"
#include "devices/disk.h"

#include "lib/kernel/bitmap.h"

#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init (void);
bool swap_in (void *frame_addr, disk_sector_t sector_num);
disk_sector_t swap_out (void *frame_addr);
void read_from_disk (void *frame_addr, disk_sector_t sector_num);
void write_to_disk (void *frame_addr, disk_sector_t sector_num);
disk_sector_t get_empty_sector_num(void);
/* The swap device */
struct disk *swap_device;

/* Tracks in-use and free swap slots */
struct bitmap *swap_table;

/* Protects swap_table */
struct lock swap_lock;




#endif /* vm/swap.h */