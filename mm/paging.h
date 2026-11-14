/*
 * mm/paging.h — Paginazione: la traduzione da indirizzi virtuali a indirizzi fisici.
 * Il codice è in mm/paging.c.
 */
#ifndef MM_PAGING_H
#define MM_PAGING_H

#include <stdint.h>

/*
 * Permessi di una pagina: sono i bit bassi di ogni riga delle tabelle.
 * Si combinano con l'OR: PAGE_WRITABLE | PAGE_USER.
 */
#define PAGE_PRESENT  0x1   /* bit 0: la pagina esiste (se 0, ogni accesso dà page fault)   */
#define PAGE_WRITABLE 0x2   /* bit 1: si può scrivere (se 0, sola lettura)                  */
#define PAGE_USER     0x4   /* bit 2: accessibile anche in modalità utente (servirà in fase 5) */

/* Costruisce le tabelle delle pagine del kernel e accende la paginazione. */
void paging_init(void);

/*
 * Fa corrispondere la pagina virtuale `virtual_address` alla pagina fisica
 * `physical_address`, con i permessi `flags` (PAGE_PRESENT viene aggiunto da solo).
 * Entrambi gli indirizzi devono essere multipli di 4096.
 * Restituisce 1 se riesce, 0 se manca la memoria per una nuova tabella.
 */
int paging_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags);

/* Cancella la corrispondenza: da qui in poi accedere a quella pagina dà page fault.
 * NON libera la pagina fisica: quello spetta a chi l'aveva allocata. */
void paging_unmap_page(uint32_t virtual_address);

/* Traduce "a mano" un indirizzo virtuale nel fisico corrispondente, come fa la CPU.
 * Restituisce 0 se la pagina non è mappata. */
uint32_t paging_get_physical(uint32_t virtual_address);

#endif /* MM_PAGING_H */
