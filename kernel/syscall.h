/*
 * kernel/syscall.h — Le chiamate di sistema, lato kernel.
 * Il codice è in kernel/syscall.c.
 */
#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

/* Registra il gestore dell'interrupt 0x80. Va chiamata dopo interrupts_init(). */
void syscall_init(void);

#endif /* KERNEL_SYSCALL_H */
