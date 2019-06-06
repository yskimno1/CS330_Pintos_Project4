#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/cache.h"
#include "filesys/inode.h"
#include "threads/thread.h"


/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
struct dir* 
parse_dir (const char *name){
  char* name_copy = (char*) malloc(strlen(name)+1);
  char* dir_name=NULL;
  char* next_dir=NULL;
  char* saveptr;
  struct dir* dir;
  strlcpy(name_copy, name, strlen(name)+1);
  
  /* open base directory */
  // printf("namecopy : %s\n", name_copy);
  if (*name_copy=='/'){    // '/' means root directory 
    dir = dir_open_root();
    printf("open root done\n");
  }
  else if (thread_current()->current_dir == NULL){ // if NULL, root as default

    dir = dir_open_root();
    printf("open root done2\n");
  }
  else{
    dir = dir_reopen(thread_current()->current_dir);

  }

  /* open directory as parsing */
  dir_name = strtok_r(name_copy, "/",&saveptr);

  if (dir_name != NULL)
    next_dir = strtok_r(NULL, "/",&saveptr);

  if(dir_name == NULL && next_dir == NULL){
    free(name_copy);
    return dir;
  }

  // printf("next dir, dir name at start: %s, %s\n", next_dir, dir_name);
  while(next_dir != NULL && dir != NULL){

    struct inode* inode;
    if (strcmp(dir_name, ".")==0){
      dir_name = next_dir;
      next_dir = strtok_r(NULL, "/", &saveptr);
      continue;
    }
    else if (strcmp(dir_name, "..")==0){
      inode = dir_get_parent_inode(dir);
      if(inode == NULL) return NULL; 
    }
    else{
      // printf("dir_name : %s\n", dir_name);
      if (dir_lookup(dir, dir_name, &inode) == false){

        free(name_copy);
        return NULL;
      }

      // printf("inode sector : %d, parent %d, dir : %d\n", inode_get_inumber(inode), inode_parent(inode), inode_is_dir(inode));
      if(inode_is_dir(inode)){
        // printf("inode is directory\n");
        dir_close(dir);
        dir = dir_open(inode);
      }
      else{
        inode_close(inode);
      }
    }
    dir_name = next_dir;
    next_dir = strtok_r(NULL, "/",&saveptr);

    // printf("next dir, dir name at bottom: %s, %s\n", next_dir, dir_name);
  }
  free(name_copy);
  return dir;
}


char* 
parse_file(const char *name){

  char* name_copy = (char*) malloc(strlen(name)+1);
  char* filename;
  char* token;
  char* next_token;
  char* saveptr;
  strlcpy(name_copy, name, strlen(name)+1);
  next_token = strtok_r(name_copy, "/",&saveptr);
  while(next_token != NULL){
    token = next_token;
    next_token = strtok_r(NULL, "/", &saveptr);
  }

  char* result = (char* )malloc(strlen(token)+1);
  strlcpy(result, token, strlen(token)+1);

  free(name_copy);
  return result;
}


void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();
  cache_init();

  if (format) 
    do_format ();

  // printf("filesys init \n");

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  // printf("filesys done\n");
  cache_write_behind_loop();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  // printf("filesys create\n");
  disk_sector_t inode_sector = 0;
  // struct dir *dir = dir_open_root ();
  struct dir* dir = parse_dir(name);
  char* filename = parse_file(name);

  if (dir==NULL || !strcmp(name, "")){
    dir_close (dir);
    free(filename);
    return false;
  }

  // printf("parse done\n");
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, filename, inode_sector));
  
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  dir_close (dir);
  free(filename);
  // printf("create done\n");

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  // struct dir *dir = dir_open_root ();
  if(strlen(name)==0) return NULL;

  struct dir* dir = parse_dir(name);
  bool passed = false;
  struct inode *inode = NULL;
  if(dir==NULL) passed = false;
  if(dir != NULL){
    char* filename = parse_file(name);
    if(strcmp(filename, ".")==0){
      free(filename);
      return (struct file* )dir;
    }
    else if(inode_get_inumber(dir_get_inode(dir))==ROOT_DIR_SECTOR && strlen(filename)==0){
      free(filename);
      return (struct file* )dir;
    }
    else if(strcmp(filename, "..")==0){
      struct inode* inode_parent;
      inode_parent = dir_get_parent_inode(dir);
      if(inode_parent == NULL){
        free(filename);
        return NULL; 
      }
      else passed=true;
    }
    dir_lookup(dir, filename, &inode);
    free(filename);
  }
  dir_close(dir);

  if(inode == NULL) return NULL;
  else{
    if(inode_is_dir(inode)) return (struct file* )dir_open(inode);
    else return file_open(inode);
  }


  // if (dir != NULL)
  //   dir_lookup (dir, name, &inode);
  // dir_close (dir);

  // return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir* dir = parse_dir(name);
  char* filename = parse_file(name);
  // struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, filename);
  // bool success = dir != NULL && dir_remove (dir, name);
  free(filename);
  dir_close (dir);
 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
