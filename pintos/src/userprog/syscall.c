#include "userprog/syscall.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall-nr.h> // syscall names
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/init.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "userprog/process.h"
#include "filesys/off_t.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/inode.h"
// struct file 
//   {
//     struct inode *inode;        /* File's inode. */
//     off_t pos;                  /* Current position. */
//     bool deny_write;            /* Has file_deny_write() been called? */
//   };
typedef int pid_t;

static void syscall_handler (struct intr_frame *);
static uint32_t* p_argv(void* addr);
static void halt (void);
static pid_t exec (const char *file);
static int wait (pid_t pid);
static int create (const char *file, unsigned initial_size, void* esp);
static int remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size, void* esp);
static int write (int fd, const void *buffer, unsigned size, void* esp);
static void seek (int fd, unsigned position);
static int tell (int fd);
static void close (int fd);
static int mmap (int fd, void* addr);
void munmap(int mapid);
static bool fd_validate(int fd);
static bool string_validate(const char* ptr);
static bool is_bad_pointer(const char* ptr);
static bool chdir (const char *dir);
static bool mkdir (char *dir);
static bool readdir (int fd, char *name);
static bool isdir (int fd);
static int inumber (int fd);

struct file_entry{
	int fd;
	struct file* file;
	struct list_elem elem_file;
};

bool
list_compare_fd(const struct list_elem* a, const struct list_elem* b, void* aux){
    struct file_entry* fe_1 = list_entry(a, struct file_entry, elem_file);
    struct file_entry* fe_2 = list_entry(b, struct file_entry, elem_file);
    return (fe_1->fd < fe_2->fd); 
}

void
list_remove_by_fd(int fd){
	struct list_elem* e;
	int matched = 0;
	struct thread* curr = thread_current();
	struct file_entry* fe;

	if(!list_empty(&curr->list_file)){
			for(e=list_begin(&curr->list_file); e!=list_end(&curr->list_file); e = list_next(e)){
					fe = list_entry(e, struct file_entry, elem_file);
					if(fe->fd == fd){
						matched = 1;
						break;
					} 
			}
	}
	if(matched) list_remove(e);
	else		return;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
//   filelock_init();
}

static void
syscall_handler (struct intr_frame *f) 
{
  void* if_esp = f->esp;
  if(is_kernel_vaddr(if_esp)){
	if(!thread_current()->is_exited) file_close(thread_current()->main_file);
    thread_exit(); 
    return;
  }

  int syscall_func = *(uint32_t* )if_esp;
  uint32_t argv0;
  uint32_t argv1;
  uint32_t argv2;
  switch(syscall_func)
  	{
 		case SYS_HALT:		/* Halt the operating system. */
    	halt();
  		break;

  	case SYS_EXIT:		/* Terminate this process. */
  		argv0 = *p_argv(if_esp+4);
  		exit((int)argv0);
  		break;

  	case SYS_EXEC:		/* Start another process. */
  		argv0 = *p_argv(if_esp+4);
  		f->eax = (uint32_t) exec((const char *)argv0);
  		break;

  	case SYS_WAIT:		/* Wait for a child process to die. */
  		argv0 = *p_argv(if_esp+4);
  		f->eax = wait((pid_t)argv0);
  		break;

  	case SYS_CREATE:	/* Create a file. */
  		argv0 = *p_argv(if_esp+4);
      argv1 = *p_argv(if_esp+8);

			// filelock_acquire();
			int result = create((const char*)argv0, (unsigned)argv1, if_esp);
			// filelock_release();
			if(result == -1){
				exit(-1);
				break;
			}
			else{
				f->eax = (bool)result;
				break;
			}

  	case SYS_REMOVE:	/* Delete a file. */
  		argv0 = *p_argv(if_esp+4);
			// filelock_acquire();
			result = remove((const char* )argv0);
			// filelock_release();
			if(result == -1){
				exit(-1);
				break;
			}
			else{
				f->eax = (bool)result;
				break;
			}

  	case SYS_OPEN:		/* Open a file. */
  		argv0 = *p_argv(if_esp+4);
			result = open((const char *)argv0);
			f->eax = result;
			break;

  	case SYS_FILESIZE:/* Obtain a file's size. */
  		argv0 = *p_argv(if_esp+4);
			// filelock_acquire();
			result = filesize((int)argv0);
			// filelock_release();
			if(result == -1){
				exit(-1);
				break;
			}
			else{
				f->eax = result;
				break;
			}

  	case SYS_READ:		/* Read from a file. */
  		argv0 = *p_argv(if_esp+4);
      argv1 = *p_argv(if_esp+8);
      argv2 = *p_argv(if_esp+12);
			f->eax = read((int)argv0, (void *)argv1, (unsigned)argv2, if_esp);
  		break;

  	case SYS_WRITE:		/* Write to a file. */
      argv0 = *p_argv(if_esp+4);
      argv1 = *p_argv(if_esp+8);
      argv2 = *p_argv(if_esp+12);
  		f->eax = write((int)argv0, (void *)argv1, (unsigned)argv2, if_esp);
  		break;

  	case SYS_SEEK:		/* Change position in a file. */
      argv0 = *p_argv(if_esp+4);
      argv1 = *p_argv(if_esp+8);
			seek((int)argv0, (unsigned)argv1);
			if(result == -1){
				exit(-1);
				break;
			}
			else{
				f->eax = result;
				break;
			}

  	case SYS_TELL:		/* Report current position in a file. */
  		argv0 = *p_argv(if_esp+4);
			result = tell((int)argv0);
			if(result == -1){
				exit(-1);
				break;
			}
			else{
				f->eax = result;
				break;
			}

  	case SYS_CLOSE:
  		argv0 = *p_argv(if_esp+4);
			close((int)argv0);
			break;


		case SYS_MMAP:
			argv0 = *p_argv(if_esp+4);
			argv1 = *p_argv(if_esp+8);

			f->eax = mmap((int)argv0, (void *)argv1);
			break;

		case SYS_MUNMAP:
			argv0 = *p_argv(if_esp+4);

			munmap((int)argv0);
			break;
		case SYS_CHDIR:      /* Open a file. */
			argv0 = *p_argv(if_esp+4);
			f->eax = chdir((const char *)argv0);
			break;

		case SYS_MKDIR:      /* Open a file. */
			argv0 = *p_argv(if_esp+4);
			f->eax = mkdir((char *)argv0);
			break;

		case SYS_READDIR:
			argv0 = *p_argv(if_esp+4);
			argv1 = *p_argv(if_esp+8);
			f->eax = readdir((int)argv0, (char *)argv1);
			break;

		case SYS_ISDIR:      /* Open a file. */
			argv0 = *p_argv(if_esp+4);
			f->eax = isdir((int)argv0);
			break;

		case SYS_INUMBER:      /* Open a file. */
			argv0 = *p_argv(if_esp+4);
			f->eax = inumber((int)argv0);
			break;
  	default:
		  printf("other syscall came!\n");
			ASSERT(0);
  		break;
  	}
}

uint32_t* 
p_argv(void* addr){

  if (addr==NULL)		exit(-1);
	
  if (!is_user_vaddr(addr) || addr < STACK_BOTTOM)	exit(-1);
	
  return (uint32_t *)(addr);
}

void
check_page(void* buffer, unsigned size, void* esp){
	void* ptr = buffer;
	for(;ptr<buffer+size; ptr++){

		if (is_bad_pointer(ptr)){
			struct sup_page_table_entry* spt_e = find_page(ptr);
			
			if(spt_e != NULL){
				bool success = page_handling(spt_e);
				if(success == false) ASSERT(0);
			}

			if(ptr >= esp - 32){

				bool success = grow_stack(ptr, PAGE_FAULT);

				if(success == false){
					// filelock_release();
					exit(-1);
				}
			}
		}

	}
}

void 
halt (void){
	power_off();
}

void 
exit (int status){
  struct thread* t = thread_current();
  t->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	int i; 
	// if(!thread_current()->is_exited) file_close(thread_current()->main_file);

	thread_exit ();
} 

pid_t 
exec (const char *cmd_line){
  if (!string_validate(cmd_line))	exit(-1);
	tid_t pid = process_execute (cmd_line);
  return pid;
}

int wait (pid_t pid){
	return process_wait(pid);
}

int create (const char *file, unsigned initial_size, void* esp){

  if (strcmp(file, "") && !string_validate(file)){
	// filelock_release();
    exit(-1);
  }

  if (strlen(file)>14){
    return 0;
	}

	check_page(file, initial_size, esp);

	return filesys_create(file, initial_size, false); 
}

int remove (const char *file){
  if (!string_validate(file) || strlen(file)>14){
    return -1;
  }
	return filesys_remove(file);
}

int open (const char *file){
  if (!string_validate(file) || strlen(file)>14) return -1;
	
	// filelock_acquire();
	struct file* f = filesys_open(file);
	if (f == NULL) {
		// filelock_release();
		return -1;
	}
	struct file_entry* fe = malloc(sizeof(struct file_entry));
	
//   filelock_release();
  struct thread *t = thread_current();
  int fd = (t->fd_vld)++;
  t->fdt[fd] = f;

  return fd; 
}

int filesize (int fd){
  if (!fd_validate(fd)){
    return -1;
  }
	return file_length(thread_current()->fdt[fd]);
}

int read (int fd, void *buffer, unsigned size, void* esp){
	// filelock_acquire();
	int cnt=-1; unsigned i;
	char* buffer_pointer = buffer;
	if (!fd_validate(fd)){
		// filelock_release();
		return -1;
	}
  if (!string_validate(buffer)){
		// filelock_release();
		exit(-1);
    return -1;
	}
	check_page(buffer, size, esp);

	if (fd == 0){			//keyboard input
		for (i=0; i<size; i++) {
			buffer_pointer[i] = input_getc();
		}
		cnt=size;
		// filelock_release();
		return size;
	}

	else {
		struct thread* t = thread_current();
		if (t->fdt[fd]==NULL)
			cnt = -1;
		else{
			cnt = file_read(t->fdt[fd], buffer, size);
		}
	}
	// filelock_release();
	return cnt;
}

int write (int fd, const void *buffer, unsigned size, void* esp){

  int cnt=-1;
  if (!fd_validate(fd))			return cnt;

  if (!string_validate(buffer)){
		exit(-1);
    return cnt;
	}
	check_page(buffer, size, esp);

	if (fd ==0){
		exit(-1);
		return -1;
	}

	if (fd == 1){
		putbuf (buffer, size);
    	return size;  
	}

	struct thread* t = thread_current();
	struct file* f = t->fdt[fd];
	if(f==NULL) return -1;
	// filelock_acquire();
	cnt = file_write(f, buffer, size);	
	// filelock_release();
	return cnt;
}

void seek (int fd, unsigned position){
	if (!fd_validate(fd))		return;
	struct file* f = thread_current()->fdt[fd];
  file_seek (f, position);  
}

int tell (int fd){
	if (!fd_validate(fd))		return -1;
	struct file* f = thread_current()->fdt[fd];
	return file_tell(f);
}

void close (int fd){
	if (!fd_validate(fd)){
		exit(-1);
		return;
	}
	// filelock_acquire();
	struct thread* t = thread_current();
	struct file* f = t->fdt[fd];
	t->fdt[fd] = NULL;
	file_close(f);
	// filelock_release();
}

int mmap(int fd, void* addr){ //needs lazy loading
	// filelock_acquire();
	struct thread* curr = thread_current();
	struct file* f = curr->fdt[fd];
	if(f == NULL){
		// filelock_release();
		return -1;
	}
	if(pg_ofs(addr)!=0 || addr==0){
		// filelock_release();
		return -1;
	}

	struct file* f_reopen = file_reopen(f);
	if(f_reopen == NULL){
		// filelock_release();
		return -1;
	}

	uint32_t read_bytes = file_length(f_reopen);
	if(read_bytes == 0){
		// filelock_release();
		return -1;
	}
	uint32_t zero_bytes = 0;
	off_t offset = 0;

	while(read_bytes > 0){
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		// if(pg_ofs(offset)!=0) break;
		struct sup_page_table_entry* spt_e = find_page(addr);

		if(spt_e == NULL){

			spt_e = allocate_page(addr, false, CREATE_MMAP, page_read_bytes, page_zero_bytes, f_reopen, offset, true);
			if(spt_e == NULL){

				// filelock_release();
				return -1;
			}

			struct page_mmap* mmap_e = malloc(sizeof(struct page_mmap));
			if(mmap_e == NULL){
				free(spt_e);

				// filelock_release();
				return -1;
			}

			mmap_e->spt_e = spt_e;
			mmap_e->spt_e->map_id = thread_current()->map_id;
			list_push_back(&thread_current()->list_mmap, &mmap_e->elem_mmap);

			bool success = page_insert(spt_e);
			if(success == false){
				list_remove(&mmap_e->elem_mmap);

				// filelock_release();
				return -1;
			}
		}
		else{
			struct page_mmap* mmap_e = malloc(sizeof(struct page_mmap));
			if(mmap_e == NULL){
				free(spt_e);

				// filelock_release();
				return -1;
			}

			mmap_e->spt_e = spt_e;
			mmap_e->spt_e->map_id = thread_current()->map_id;
			list_push_back(&thread_current()->list_mmap, &mmap_e->elem_mmap);

			bool success = page_insert(spt_e);
			if(success == false){
				list_remove(&mmap_e->elem_mmap);

				// filelock_release();
				return -1;
			}
		}

		/* do we need to check other mmaps? */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		offset += page_read_bytes;
		addr += PGSIZE;
	}

	int return_value = thread_current()->map_id;
	thread_current()->map_id += 1;
	// filelock_release();
	return return_value;
}

void munmap(int mapid){

	struct list_elem* e;
	e = list_begin(&thread_current()->list_mmap);
	while(e!=list_end(&thread_current()->list_mmap)){
		struct page_mmap* mmap_e = list_entry(e,struct page_mmap, elem_mmap);
		if(mmap_e->spt_e->map_id == mapid){
			// printf("begin! map id : %d, addr %p\n", mapid, mmap_e->spt_e);
			if(pagedir_is_dirty(thread_current()->pagedir, mmap_e->spt_e->user_vaddr)){
				// filelock_acquire();
				file_write_at(mmap_e->spt_e->file, mmap_e->spt_e->user_vaddr, mmap_e->spt_e->read_bytes, mmap_e->spt_e->offset);
				// filelock_release();
				free_frame(pagedir_get_page(thread_current()->pagedir, mmap_e->spt_e->user_vaddr));
				free_page(&mmap_e->spt_e->elem);
				e = list_remove(e);
			}
			else{
				free_frame(pagedir_get_page(thread_current()->pagedir, mmap_e->spt_e->user_vaddr));
				free_page(&mmap_e->spt_e->elem);
				e = list_remove(e);
			}
		}
		else	e = list_next(e);
	}

	return;
}

bool chdir (const char *dir){
  struct dir* dir_location = parse_dir(dir);
  char* filename = parse_file(dir);
  struct inode *inode = NULL;
  struct dir* obj_dir = thread_current()->current_dir;
	
	if (dir_location==NULL){
		free(filename);
		return false;
	}

	if (strcmp(filename, ".")==0){
		obj_dir = dir_location;
		thread_current()->current_dir = obj_dir;
		return true;
	}
	else if(inode_get_inumber(dir_get_inode(dir_location))==ROOT_DIR_SECTOR && strlen(filename)==0){
		obj_dir = dir_location; // root condition
		thread_current()->current_dir = obj_dir;
		return true;
	}
	else if(strcmp(filename, "..")==0){
		obj_dir = dir_open_parent(dir_location);
		if(obj_dir==NULL){
			free(filename);
			return false;
		}
	}
	else{
		dir_lookup (dir_location, filename, &inode);
		if (inode==NULL){
				dir_close(dir_location);
				free(filename);
				return false;
		}
		if (!inode_is_dir(inode)){
				dir_close(dir_location);
				free(filename);
				return false;
		}
		dir_close(dir_location);

		/* new open - where thread wants to go */
		struct dir* dir_curr = dir_open(inode);
		if(dir_curr == NULL){
			free(filename);
			return false;
		}
		else{
			obj_dir = dir_curr;
			thread_current()->current_dir = obj_dir;
			free(filename);
			return true;
		}
	}
}

bool mkdir (char* dir){
   return filesys_create(dir, 0, true);   
}

bool readdir (int fd, char *name){
   if (!fd_validate(fd))
      return false;
   struct file* file;
   struct inode* inode;
   struct dir* dir;
   file = thread_current()->fdt[fd];
   if (file==NULL) return false;

   inode = file_get_inode(file);
   if (inode==NULL) return false;
   if (!inode_is_dir(inode)) return false;

   dir = (struct dir*)file;
   return dir_readdir(dir, name);
}

bool isdir (int fd){
   if (!fd_validate(fd))
      return false;

   struct file* file = thread_current()->fdt[fd];
   if (file==NULL) return false;

   struct inode* inode = file_get_inode(file);
   if (inode==NULL) return false;

   return inode_is_dir(inode);
}

int inumber (int fd){
   if (!fd_validate(fd))
      return false;

   struct file* file = thread_current()->fdt[fd];
   if (file==NULL) return false;

   const struct inode* inode = file_get_inode(file);
   if (inode==NULL) return false;

   return inode_get_inumber(inode);
}


bool
fd_validate(int fd){
	struct thread* t = thread_current();
	bool val = true;
	val = val && fd>=0 && fd<FILE_MAX && (fd < (t->fd_vld));
	if (fd >2 )
		val = val && t->fdt[fd] != NULL;
	return val;
}

bool
string_validate(const char* ptr){
	if (!is_user_vaddr(ptr))  return false;
  if (ptr == NULL) 					return false;
	if (strcmp(ptr, "")==0)		return -1;
	
  return true;
}

bool
is_bad_pointer(const char* ptr){
	if(is_kernel_vaddr(ptr)) return false;
	void* ptr_page = pagedir_get_page(thread_current()->pagedir, ptr);
	if(!ptr_page)				return true;
	else 								return false;
}