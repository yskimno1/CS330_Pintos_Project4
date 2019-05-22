#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void exit (int status);
#define STACK_BOTTOM (void *)0x08048000
#endif /* userprog/syscall.h */