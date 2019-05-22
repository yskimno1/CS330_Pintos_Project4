#include "vm/swap.h"
#include "devices/disk.h"
#include "threads/synch.h"
// #include <bitmap.h>
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"

void 
swap_init (void)
{

    swap_device = disk_get(1,1);

    swap_table = bitmap_create((disk_size(swap_device)*DISK_SECTOR_SIZE)/PGSIZE); // kys
    if(swap_table == NULL) ASSERT(0);

    bitmap_set_all(swap_table, 0);
    lock_init(&swap_lock);
}

bool 
swap_in (void *frame_addr, disk_sector_t sector_num)
{ 
    // printf("swap in\n");
    lock_acquire(&swap_lock);
    disk_sector_t bitmap_idx = (sector_num * DISK_SECTOR_SIZE)/PGSIZE;
    bool success = bitmap_test(swap_table, bitmap_idx);
    if(success == false) PANIC("invalid swap space!");

    bitmap_flip(swap_table, bitmap_idx);
    read_from_disk(frame_addr, sector_num);

    lock_release(&swap_lock);
    return true;
}

disk_sector_t
swap_out (void* frame_addr)
{
    // printf("swap out\n");
    lock_acquire(&swap_lock);
    void* addr =pg_round_down(frame_addr);

    disk_sector_t sector_num = get_empty_sector_num();
    write_to_disk(frame_addr, sector_num);

    lock_release(&swap_lock);
    return sector_num;
}

void read_from_disk (void *frame_addr, disk_sector_t sector_num)
{
    disk_sector_t i;
    for(i=0; i<PGSIZE/DISK_SECTOR_SIZE; i++){
        void* dst = i*DISK_SECTOR_SIZE + frame_addr;
        disk_read(swap_device, i+sector_num, dst);
    }
    return;
}

/* Write data to swap device from frame */
void write_to_disk (void *frame_addr, disk_sector_t sector_num)
{
    disk_sector_t i;
    for(i=0; i<PGSIZE/DISK_SECTOR_SIZE; i++){    
        void* dst = i*DISK_SECTOR_SIZE + frame_addr; // total num is PGSIZE
        disk_write(swap_device, i+sector_num, dst);
    }
    return;
}

disk_sector_t
get_empty_sector_num(void){

    size_t bitmap_idx = bitmap_scan_and_flip(swap_table, 0, 1, 0);
    // printf("bitmapidx : %d\n", bitmap_idx);
    if(bitmap_idx != BITMAP_ERROR) return (bitmap_idx * PGSIZE)/DISK_SECTOR_SIZE;
    else{
        PANIC("bitmap full");
    }
}