#include "vm/page.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include "threads/thread.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
// #include "filesys/directory.h"
// #include "filesys/filesys.h"

// /* true if A is less than B */
// bool
// list_less(const struct list_elem* a, const struct list_elem* b, void* aux){
//     struct sup_page_table_entry* spt_e_1 = list_entry(a, struct sup_page_table_entry, elem);
//     struct sup_page_table_entry* spt_e_2 = list_entry(b, struct sup_page_table_entry, elem);
//     return (spt_e_1->user_vaddr < spt_e_1->user_vaddr); // not using hash_func b/c all user_vaddr are different
// }

bool
page_insert(struct sup_page_table_entry* spt_e){

    struct thread* curr = thread_current();
    struct list_elem* e;
    if(!list_empty(&curr->sup_page_table)){

        for(e=list_begin(&curr->sup_page_table); e!=list_end(&curr->sup_page_table); e = list_next(e)){
            struct sup_page_table_entry* temp = list_entry(e, struct sup_page_table_entry, elem);
            if(temp->user_vaddr == spt_e->user_vaddr){
                // ASSERT(0);
                // printf("same!\n");
                return false;
            }
        }
    }
    list_push_back(&curr->sup_page_table, &spt_e->elem);
    // printf("inserted! current length : %d, table address %p\n", list_size(&curr->sup_page_table), &curr->sup_page_table);
    return true;
    //list_insert_ordered(&curr->sup_page_table, &spt_e->elem, list_less, 0);
}

/*
 * Make new supplementary page table entry for addr 
 */
struct sup_page_table_entry* 
allocate_page (void* addr, bool loaded, enum palloc_type p_type, uint32_t read_bytes, uint32_t zero_bytes, struct file* file, int32_t offset, bool writable){
    if(is_kernel_vaddr(addr)) ASSERT(0);
    struct sup_page_table_entry* spt_e = malloc(sizeof(struct sup_page_table_entry));
    if(spt_e == NULL) return NULL;
    if(p_type == GROW_STACK || p_type == PAGE_FAULT){
        spt_e->user_vaddr = addr;
        spt_e->loaded = loaded;
        spt_e->writable = writable;
        spt_e->file_type = TYPE_SWAP;
    }
    else if(p_type == LOAD_SEGMENT || p_type == CREATE_MMAP){
        if(p_type == CREATE_MMAP){
            spt_e->file_type = TYPE_MMAP;
            spt_e->map_id = thread_current()->map_id;
        }
        spt_e->user_vaddr = addr;
        spt_e->loaded = loaded;
        spt_e->read_bytes = read_bytes;
        spt_e->zero_bytes = zero_bytes;
        spt_e->offset = offset;
        spt_e->file = file;
        spt_e->writable = writable;
        spt_e->file_type = TYPE_FILE;

    }
    else ASSERT(0);
    
    spt_e->is_swapped = false;
    return spt_e;
}

struct sup_page_table_entry*
find_page(void* addr){

    void* aligned_addr = pg_round_down(addr);
    struct sup_page_table_entry* spt_e;

    struct list_elem* e;
    struct thread* curr = thread_current();

    if(!list_empty(&curr->sup_page_table)){
        for(e=list_begin(&curr->sup_page_table); e!=list_end(&curr->sup_page_table); e = list_next(e)){
            spt_e = list_entry(e, struct sup_page_table_entry, elem);

            if(spt_e->user_vaddr == aligned_addr) return spt_e;
        }
    }

    return NULL;
}

void*
free_page(struct list_elem* e){
    struct sup_page_table_entry* spt_e = list_entry(e, struct sup_page_table_entry, elem);
    if(spt_e->loaded){
        struct thread* curr = thread_current();
        free_frame(pagedir_get_page(curr->pagedir, spt_e->user_vaddr));
        pagedir_clear_page(curr->pagedir, spt_e->user_vaddr);
    }
    free(spt_e);
}

bool
page_handling(struct sup_page_table_entry* spt_e){

    lock_acquire(&lock_frame);
    bool success;

    if(spt_e->file_type == TYPE_FILE){
        if(spt_e->loaded == true) success = swap_handling(spt_e);
        else success = file_handling(spt_e);
    }
    else if(spt_e->file_type == TYPE_MMAP) success = file_handling(spt_e);
    else if(spt_e->file_type == TYPE_SWAP) success = swap_handling(spt_e);
    lock_release(&lock_frame);

    return success;
}

bool
file_handling(struct sup_page_table_entry* spt_e){

    void* frame;
    if(spt_e->read_bytes == 0) frame = allocate_frame(spt_e, PAL_USER|PAL_ZERO);
    else frame = allocate_frame(spt_e, PAL_USER);
    if(frame == NULL) return false;

    if(spt_e->read_bytes > 0){
        off_t temp = file_read_at (spt_e->file, frame, spt_e->read_bytes, spt_e->offset);

        if (temp != (int) spt_e->read_bytes){
            free_frame(frame);
            ASSERT(0);
            return false; 
        }
        memset (frame + spt_e->read_bytes, 0, spt_e->zero_bytes);
    }
    bool success = install_page(spt_e->user_vaddr, frame, spt_e->writable);

    if(success == false){
        free_frame(frame);
        return false;
    }

    spt_e->loaded = true;

    return true;
}

bool
swap_handling(struct sup_page_table_entry* spt_e){
    void* frame = allocate_frame(spt_e, PAL_USER);
    if(frame == NULL){
        return false;
    }
    bool success = install_page(spt_e->user_vaddr, frame, spt_e->writable);
    if(success == false){
        free_frame(frame);
        return false;
    }
    if(spt_e->is_swapped) swap_in(frame, spt_e->swap_num);
    spt_e->is_swapped = false;
    spt_e->loaded = true;
    return true;
}

bool
grow_stack(void* addr, enum palloc_type ptype){

    void* page_addr = pg_round_down(addr);
    struct sup_page_table_entry* spt_e;
    uint8_t* frame_addr;
    if(ptype == PAGE_FAULT){
        spt_e = allocate_page(page_addr, true, PAGE_FAULT, 0, 0, NULL, 0, 1); // already loaded
        frame_addr = allocate_frame(spt_e, PAL_USER);
    }
    else if(ptype == GROW_STACK){
        spt_e = allocate_page(page_addr, true, GROW_STACK, 0, 0, NULL, 0, 1);
        frame_addr = allocate_frame(spt_e, PAL_USER|PAL_ZERO);
    }
    if(frame_addr==NULL){
        free(spt_e);
        return false;
    }

    bool success = install_page(page_addr, frame_addr, true);
    if(success == false){
        free_frame(frame_addr);
        free(spt_e);
        return false;
    }
    success = page_insert(spt_e);
    return success;
}
