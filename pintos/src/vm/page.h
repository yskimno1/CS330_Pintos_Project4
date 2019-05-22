#include <stdint.h>
#include <stdbool.h>
#include <hash.h>

#ifndef VM_PAGE_H
#define VM_PAGE_H

// struct list sup_page_table;
#define LIMIT (1 << 23)
struct sup_page_table_entry 
{
	uint32_t* user_vaddr;
	uint64_t access_time;

	uint32_t read_bytes;
	uint32_t zero_bytes;

	struct list_elem elem;

	bool accessed;
	bool is_swapped;

	struct file* file;
	int32_t offset;
	bool writable;
	bool loaded;

	int fd_num;
	int file_type;

	int map_id;
	int swap_num;
};

struct page_mmap{
    struct sup_page_table_entry* spt_e;
    struct list_elem elem_mmap;
};

enum TYPE_FILE{ // change name //
	TYPE_SWAP = 0,
	TYPE_MMAP = 1,
	TYPE_FILE = 2
};

enum palloc_type{
    GROW_STACK=0,
	PAGE_FAULT=1,
	LOAD_SEGMENT=2,
	CREATE_MMAP = 3 
};


bool list_less(const struct list_elem* a, const struct list_elem* b, void* aux);
bool page_insert(struct sup_page_table_entry* spt_e);
void page_init (void);
struct sup_page_table_entry*  allocate_page(void *addr, bool loaded, enum palloc_type p_type, uint32_t read_bytes, uint32_t zero_bytes, struct file *file, int32_t offset, bool writable);
void page_done(void);
struct sup_page_table_entry* find_page(void* addr);
void* free_page(struct list_elem* e);
bool grow_stack(void* addr, enum palloc_type ptype);

bool page_handling(struct sup_page_table_entry* spt_e);
bool swap_handling(struct sup_page_table_entry* spt_e);
bool file_handling(struct sup_page_table_entry* spt_e);
bool mmap_handling(struct sup_page_table_entry* spt_e);
#endif /* vm/page.h */
