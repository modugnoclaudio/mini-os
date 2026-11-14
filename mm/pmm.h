/*
 * mm/pmm.h — PMM = Physical Memory Manager, l'allocatore della memoria fisica.
 * Il codice è in mm/pmm.c.
 *
 * Tiene il conto di quali "pagine fisiche" (dette anche frame) da 4 KiB sono libere
 * e le distribuisce a chi le chiede.
 */
#ifndef MM_PMM_H
#define MM_PMM_H

#include <stdint.h>
#include "kernel/multiboot.h"

/* Dimensione di una pagina su x86: 4096 byte = 4 KiB. È la stessa sia per la
 * memoria fisica (frame) sia per quella virtuale (pagine). */
#define PAGE_SIZE 4096

/* Quanta memoria fisica gestiamo al massimo: 1 GiB. La RAM oltre questo limite
 * viene ignorata. Il `u` finale dice che la costante è senza segno (unsigned). */
#define PMM_MAX_MEMORY 0x40000000u

/* Legge la mappa della memoria Multiboot, la stampa e prepara la bitmap. */
void pmm_init(struct multiboot_info *mbi);

/* Riserva una pagina fisica e ne restituisce l'INDIRIZZO FISICO (multiplo di 4096).
 * Restituisce 0 se la memoria è finita. Il contenuto della pagina NON è azzerato. */
uint32_t pmm_alloc_page(void);

/* Restituisce al sistema una pagina ottenuta con pmm_alloc_page(). */
void pmm_free_page(uint32_t address);

/* Statistiche. */
uint32_t pmm_get_free_pages(void);      /* pagine libere in questo momento          */
uint32_t pmm_get_memory_top(void);      /* indirizzo dove finisce la RAM utilizzabile */

#endif /* MM_PMM_H */
