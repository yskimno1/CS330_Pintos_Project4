#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define NUM_PTRS 15
#define NUM_PTRS_DIR 5
#define NUM_PTRS_INDIR 8
#define NUM_PTRS_DOUBLE 2

#define FILE_SIZE_MAX 1<<23

#define PTR_PER_BLOCK 128 // 512/4

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */

    unsigned is_allocated;
    disk_sector_t start;                /* First data sector. */
    disk_sector_t ptrs[NUM_PTRS];
    unsigned ptr_idx;
    unsigned indir_idx;
    unsigned double_indir_idx;

    uint32_t unused[106];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    // struct inode_disk data;             /* Inode content. */

    off_t length;
    off_t length_shown;

    unsigned is_allocated;
    disk_sector_t start;
    disk_sector_t ptrs[NUM_PTRS];
    unsigned ptr_idx;
    unsigned indir_idx;
    unsigned double_indir_idx;

  };

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  // printf("pos : %d, sector: %d\n", pos, inode->ptrs[pos/DISK_SECTOR_SIZE]);
  int new_pos;
  unsigned idx_ptr;
  ASSERT (inode != NULL);
  if(pos < inode_length(inode)){
    if(pos < DISK_SECTOR_SIZE*NUM_PTRS_DIR){

      return inode->ptrs[pos/DISK_SECTOR_SIZE];
    }
    
    disk_sector_t inner_ptr[PTR_PER_BLOCK];
    new_pos = pos - DISK_SECTOR_SIZE*NUM_PTRS_DIR;
    if (new_pos < DISK_SECTOR_SIZE*(NUM_PTRS_INDIR * PTR_PER_BLOCK)){

      idx_ptr = NUM_PTRS_DIR + (new_pos / DISK_SECTOR_SIZE / PTR_PER_BLOCK);
      new_pos = new_pos % (DISK_SECTOR_SIZE*PTR_PER_BLOCK);
      disk_read(filesys_disk, inode->ptrs[idx_ptr], &inner_ptr);
      return inner_ptr[new_pos/DISK_SECTOR_SIZE];
    }
    /* here, big files - double indirect blocks */
    disk_sector_t double_inner_ptr[PTR_PER_BLOCK];
    new_pos = new_pos - DISK_SECTOR_SIZE*NUM_PTRS_INDIR*PTR_PER_BLOCK;

    idx_ptr = NUM_PTRS_DIR + NUM_PTRS_INDIR + (new_pos / DISK_SECTOR_SIZE / PTR_PER_BLOCK / PTR_PER_BLOCK);
    printf("idx ptr : %d\n", idx_ptr);
    disk_read(filesys_disk, inode->ptrs[idx_ptr], &inner_ptr);

    new_pos = new_pos - idx_ptr * DISK_SECTOR_SIZE * PTR_PER_BLOCK * PTR_PER_BLOCK;

    int double_idx_ptr = new_pos / DISK_SECTOR_SIZE / PTR_PER_BLOCK;
    new_pos = new_pos % (DISK_SECTOR_SIZE*PTR_PER_BLOCK);
    disk_read(filesys_disk, inner_ptr[double_idx_ptr], &double_inner_ptr);
    return double_inner_ptr[new_pos/DISK_SECTOR_SIZE];
    
    // else ASSERT(0);
  }
    // return inode->start + pos / DISK_SECTOR_SIZE;
  else{
    printf("here..\n");
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

void inode_grow(struct inode* inode, off_t length){

  disk_sector_t inner_ptr[PTR_PER_BLOCK];
  disk_sector_t double_inner_ptr[PTR_PER_BLOCK];
  static char data_default[DISK_SECTOR_SIZE];

  size_t sectors = bytes_to_sectors(length) - bytes_to_sectors(inode->length);
  // printf("grow sectors : %d, %d - %d\n", sectors, length, inode->length);
  unsigned idx = inode->ptr_idx;
  while(idx<NUM_PTRS){
    if(!(sectors > 0)) break;

    if(idx<NUM_PTRS_DIR){
      if(!free_map_allocate(1, &inode->ptrs[idx])) ASSERT(0);
      disk_write(filesys_disk, inode->ptrs[idx], data_default);
      sectors -= 1;
      idx += 1;
    }

    /* indirect level 1 */
    else if(idx<NUM_PTRS_DIR + NUM_PTRS_INDIR){
      if(inode->indir_idx==0){
        if(!free_map_allocate(1, &inode->ptrs[idx])) ASSERT(0); // check whether block is allocated
      }
      else{
        disk_read(filesys_disk, inode->ptrs[idx], &inner_ptr);
      }
      unsigned indir_idx = inode->indir_idx;
      while(indir_idx<PTR_PER_BLOCK){
        if(!(sectors > 0)) break;
        if(!free_map_allocate(1, &inner_ptr[indir_idx])) ASSERT(0);
        disk_write(filesys_disk, inner_ptr[indir_idx], data_default);

        indir_idx += 1;
        sectors -= 1;
      }
      inode->indir_idx = indir_idx;
      disk_write(filesys_disk, inode->ptrs[idx], &inner_ptr);

      if(inode->indir_idx == PTR_PER_BLOCK){
        inode->indir_idx = 0;
        idx += 1;
      }
      else  ASSERT(sectors ==0);
    }

    /* indirect level 2 */
    else if(idx<NUM_PTRS_DIR + NUM_PTRS_INDIR + NUM_PTRS_DOUBLE){
      if(inode->indir_idx==0 && inode->double_indir_idx==0){ //initial condition
        if(!free_map_allocate(1, &inode->ptrs[idx])) ASSERT(0);
      }
      else{
        disk_read(filesys_disk, inode->ptrs[idx], &inner_ptr);
      }
      unsigned indir_idx = inode->indir_idx;
      while(indir_idx<PTR_PER_BLOCK){
        if(!(sectors > 0)) break;
        if(inode->double_indir_idx==0){
          if(!free_map_allocate(1, &inner_ptr[inode->indir_idx])) ASSERT(0);
        }
        else{
          disk_read(filesys_disk, inner_ptr[inode->indir_idx], &double_inner_ptr);
        }
        unsigned double_indir_idx = inode->double_indir_idx;
        while(double_indir_idx<PTR_PER_BLOCK){
          if(!(sectors > 0)) break;
          if(!free_map_allocate(1, &double_inner_ptr[double_indir_idx])) ASSERT(0);
          disk_write(filesys_disk, double_inner_ptr[double_indir_idx], data_default);

          double_indir_idx += 1;
          sectors -= 1;
        }
        inode->double_indir_idx = double_indir_idx;
        disk_write(filesys_disk, inner_ptr[indir_idx], &double_inner_ptr);

        if(inode->double_indir_idx == PTR_PER_BLOCK){
          inode->double_indir_idx = 0;
          indir_idx += 1;
        }
        else ASSERT(0);
      }
      inode->indir_idx = indir_idx;
      disk_write(filesys_disk, inode->ptrs[idx], &inner_ptr);

      if(inode->indir_idx == PTR_PER_BLOCK){
        inode->indir_idx = 0;
        idx += 1;
      }
      else  ASSERT(sectors ==0);

    }
  }
  
  inode->ptr_idx = idx;
  inode->is_allocated = 1;
  return;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      if(length > FILE_SIZE_MAX) length = FILE_SIZE_MAX;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      /* need to allocate */
      struct inode* inode = malloc(sizeof(struct inode));
      inode->length = 0;
      inode->is_allocated = 0;
      inode->ptr_idx = 0;
      inode->indir_idx = 0;
      inode->double_indir_idx = 0;

      inode_grow(inode, length);

      memcpy(&(disk_inode->start), &(inode->start), sizeof(disk_sector_t));
      memcpy(&(disk_inode->ptrs), &(inode->ptrs), sizeof(disk_sector_t) * NUM_PTRS);
      free(inode);
      disk_inode -> is_allocated = 1;
      disk_write(filesys_disk, sector, disk_inode);

      success = true;
      /* allocate done */

      // size_t sectors = bytes_to_sectors (length);

      // if (free_map_allocate (sectors, &disk_inode->start))
      //   {
      //     disk_write (filesys_disk, sector, disk_inode);
      //     if (sectors > 0) 
      //       {
      //         static char zeros[DISK_SECTOR_SIZE];
      //         size_t i;
              
      //         for (i = 0; i < sectors; i++) 
      //           disk_write (filesys_disk, disk_inode->start + i, zeros); 
      //       }
      //     success = true; 
      //   } 
      free (disk_inode);
    }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  // printf("sector : %d\n", sector);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  struct inode_disk* inode_disk = malloc(sizeof(struct inode_disk));
  // printf("sector : %d %d\n", inode->sector, sector);
  disk_read (filesys_disk, inode->sector, inode_disk);
  inode->length = inode_disk->length;
  inode->length_shown = inode_disk->length;
  inode->start = inode_disk->start;
  inode->is_allocated = inode_disk->is_allocated;
  inode->indir_idx = inode_disk->indir_idx;
  inode->double_indir_idx = inode_disk->double_indir_idx;
  inode->ptr_idx = inode_disk->ptr_idx;
  memcpy(&(inode->ptrs), &(inode_disk->ptrs), sizeof(disk_sector_t) * NUM_PTRS );
  free(inode_disk);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          // printf("remove!\n");
          free_map_release (inode->sector, 1);
          size_t sectors = bytes_to_sectors(inode_length(inode));
          /* need to deallocate */
          if(sectors != 0){
            int index = 0;
            disk_sector_t inner_ptr[PTR_PER_BLOCK];
            disk_sector_t double_inner_ptr[PTR_PER_BLOCK];
            for(; index<NUM_PTRS; index++){
              if(sectors == 0) break;
              else if(index > inode->ptr_idx) break;

              if(index < NUM_PTRS_DIR){
                free_map_release(inode->ptrs[index], 1);
                sectors -= 1;
              }

              else if(index < NUM_PTRS_DIR + NUM_PTRS_INDIR){
                disk_read(filesys_disk, inode->ptrs[index], &inner_ptr);
                int i=0;
                for(; i<PTR_PER_BLOCK; i++){ 
                  free_map_release(inner_ptr[i], 1);
                  sectors -= 1;
                  if(sectors == 0) break;
                }
                free_map_release(inode->ptrs[index], 1);
              }

              else if(index < NUM_PTRS_DIR + NUM_PTRS_INDIR + NUM_PTRS_DOUBLE){
                disk_read(filesys_disk, inode->ptrs[index], &inner_ptr);
                int i=0;
                for(; i<PTR_PER_BLOCK; i++){
                  disk_read(filesys_disk, inner_ptr[i], &double_inner_ptr);
                  int j=0;
                  for(; j<PTR_PER_BLOCK; j++){
                    free_map_release(double_inner_ptr[j], 1);
                    sectors -= 1;
                    if(sectors == 0) break;
                  }
                  free_map_release(inner_ptr[i], 1);
                  if(sectors==0) break;
                }
                free_map_release(inode->ptrs[index], 1);
              }

              else ASSERT(0);
              index += 1;
            }
          }
          /* deallocate done */

          // free_map_release (inode->start,
          //                   bytes_to_sectors (inode->length)); 
        }
      else{
        struct inode_disk* inode_disk = malloc(sizeof(struct inode_disk));

        inode_disk->length = inode->length;
        inode_disk->start = inode->start;
        inode_disk->is_allocated = inode->is_allocated;
        inode_disk->indir_idx = inode->indir_idx;
        inode_disk->double_indir_idx = inode->double_indir_idx;
        inode_disk->ptr_idx = inode->ptr_idx;
        memcpy(&(inode_disk->ptrs), &(inode->ptrs), sizeof(disk_sector_t) * NUM_PTRS);
        disk_write(filesys_disk, inode->sector, inode_disk);

        free(inode_disk);
      }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  if(inode->length_shown <= offset) return 0;
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;


  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->length_shown - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      // printf("sector idx in read : %d\n", sector_idx);
      cache_read(sector_idx, buffer, bytes_read, sector_ofs, chunk_size);
    
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if(size+offset > inode_length(inode)){
    // printf("need to grow about %d!\n", size+offset);
    inode_grow(inode, size+offset);
    inode->length = size+offset;
  }

  // int i=0;
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      // printf("inode length : %d, offset : %d\n", inode_length(inode), offset);
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      // printf("%d, size %d: sector: %d-%d=%d, inode: %d-%d=%d, min_left=%d, sector_idx=%d\n",
      //         i,size, DISK_SECTOR_SIZE,sector_ofs,sector_left, inode_length (inode),offset,inode_left, min_left, sector_idx);


      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      // printf("sector idx in write : %d\n", sector_idx);
      cache_write(sector_idx, buffer, bytes_written, sector_ofs, chunk_size);

      if(inode->length_shown + chunk_size > inode_length(inode))  inode->length_shown = inode_length(inode);
      else inode->length_shown += chunk_size;
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;

      // i = i+1;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}
