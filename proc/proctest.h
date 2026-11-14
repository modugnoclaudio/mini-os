/*
 * proc/proctest.h — Prove automatiche di processi e scheduler.
 * Il codice è in proc/proctest.c.
 *
 * Come quelle della memoria (mm/memtest.h): ogni verifica stampa "ok" oppure "ERRORE".
 * Vanno chiamate dal processo 0, dopo process_init() e con gli interrupt abilitati.
 */
#ifndef PROC_PROCTEST_H
#define PROC_PROCTEST_H

/* Due processi che si passano la CPU volontariamente con process_yield(). */
void proctest_cooperative(void);

/* Due processi che non cedono mai la CPU (il timer gliela toglie) e uno che dorme. */
void proctest_preemptive(void);

/* Il meccanismo delle syscall (int 0x80), provato dal kernel stesso. */
void proctest_syscalls(void);

/* Stampa il riepilogo finale: quante verifiche sono fallite in tutto. */
void proctest_summary(void);

#endif /* PROC_PROCTEST_H */
