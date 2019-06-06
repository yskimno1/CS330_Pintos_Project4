#include <stdint.h>
#include <stdbool.h>

#include "threads/thread.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
/*
 * Initialize frame table
 */
void 
frame_init (void)
{

    list_init(&frame_table);
    /* Needs frame lock */
    lock_init(&lock_frame);

}

void
insert_frame_table(struct frame_table_entry* fte){
    list_push_back(&frame_table, &fte->elem_table_list);
}

struct frame_table_entry*
create_frame_table_entry(void* frame, struct frame_table_entry* spt_e){
    struct frame_table_entry* fte = malloc(sizeof(struct frame_table_entry));
    if(fte==NULL){
        return NULL;
    }

    fte->frame = frame;
    fte->owner = thread_current();
    /* spte should be initialized! */
    fte->spte = spt_e;
    // printf("spte info : %p %d %d\n", fte->spte->user_vaddr, fte->spte->read_bytes, fte->spte->writable);
    return fte;
}

/* 
 * Make a new frame table entry for addr.
 * frame should be allocated after page allocation.
 */
uint8_t*
allocate_frame (struct sup_page_table_entry* spt_e, enum palloc_flags flag)
{
    ASSERT(flag & PAL_USER);

    uint8_t* frame = palloc_get_page(flag);

    if(frame == NULL){
        // lock_acquire(&lock_frame);
        bool eviction_success = evict_frame();
        // lock_release(&lock_frame);
        if(eviction_success){
            frame = palloc_get_page(flag);
        }
        // else ASSERT(0);
    }

    struct frame_table_entry* fte = create_frame_table_entry(frame, spt_e);
    // ASSERT(fte != NULL);
    if(fte == NULL) return NULL;
    // lock_acquire(&lock_frame);
    insert_frame_table(fte);
    // lock_release(&lock_frame);
    // ASSERT(frame!=NULL);

    return frame;
}

static struct frame_table_entry*
search_frame_table_entry (void* frame){
  struct list_elem* e;
  struct frame_table_entry* fte;
  if(!list_empty(&frame_table)){
    for(e=list_begin(&frame_table); e!=list_end(&frame_table); e = list_next(e)){
      fte = list_entry(e, struct frame_table_entry, elem_table_list);
      if(fte->frame == frame) return fte;
    }
  }
  return NULL;
}

bool
evict_frame (void){
    struct list_elem* e;
    struct frame_table_entry* fte;
    if(!list_empty(&frame_table)){
        for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
            fte = list_entry(e, struct frame_table_entry, elem_table_list);
            if(!pagedir_is_accessed(fte->owner->pagedir, fte->spte->user_vaddr)){
                if(fte->spte->file_type == TYPE_MMAP) ASSERT(0);

                if(pagedir_is_dirty(fte->owner->pagedir, fte->spte->user_vaddr) || fte->spte->file_type == TYPE_SWAP){
                    fte->spte->file_type = TYPE_SWAP;
                    fte->spte->swap_num = swap_out(fte->frame);
                    fte->spte->is_swapped = true;
                }
                list_remove(&fte->elem_table_list);
                pagedir_clear_page(fte->owner->pagedir, fte->spte->user_vaddr);
                palloc_free_page(fte->frame);
                
                fte->spte->loaded = false;
                free(fte);

                return true;
            } 
            else{
                pagedir_set_accessed(fte->owner->pagedir, fte->spte->user_vaddr, false);
            }
        }

        /* second chance */
        for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)){
            fte = list_entry(e, struct frame_table_entry, elem_table_list);
            if(!pagedir_is_accessed(fte->owner->pagedir, fte->spte->user_vaddr)){
                if(pagedir_is_dirty(fte->owner->pagedir, fte->spte->user_vaddr) || fte->spte->file_type == TYPE_SWAP){
                    fte->spte->file_type = TYPE_SWAP;
                    fte->spte->swap_num = swap_out(fte->frame);
                    fte->spte->is_swapped = true;
                }

                list_remove(&fte->elem_table_list);
                pagedir_clear_page(fte->owner->pagedir, fte->spte->user_vaddr);
                palloc_free_page(fte->frame);

                fte->spte->loaded = false;
                free(fte);

                return true;
            } 
            else{
                pagedir_set_accessed(fte->owner->pagedir, fte->spte->user_vaddr, false);
            }
        }   
    }
    return false;
}

void*
free_frame (void* frame){
    struct list_elem* e;
    struct frame_table_entry* fte;
    // lock_acquire(&lock_frame);
    if(!list_empty(&frame_table)){
        for(e=list_begin(&frame_table); e!=list_end(&frame_table); e = list_next(e)){
            fte = list_entry(e, struct frame_table_entry, elem_table_list);
            if(fte->frame == frame){
                list_remove(e);
                free(fte);
                palloc_free_page(frame);
                break;
            }
        }
    }
    // lock_release(&lock_frame);
}