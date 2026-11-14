/*
 * mm/memtest.h — Prove automatiche della gestione della memoria.
 * Il codice è in mm/memtest.c.
 *
 * Ogni funzione esegue alcune operazioni, controlla il risultato e stampa
 * "ok" oppure "ERRORE". Vengono chiamate all'avvio da kernel_main.
 */
#ifndef MM_MEMTEST_H
#define MM_MEMTEST_H

void memtest_pmm(void);         /* allocatore di pagine fisiche */
void memtest_paging(void);      /* paginazione                  */

/* Stampa il riepilogo finale: quante verifiche sono fallite in tutto. */
void memtest_summary(void);

#endif /* MM_MEMTEST_H */
