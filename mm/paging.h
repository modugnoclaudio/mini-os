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
#define PAGE_USER     0x4   /* bit 2: accessibile anche in modalità utente                  */

/*
 * La zona dello spazio di indirizzi virtuale riservata ai programmi utente:
 * da 1 GiB a 3 GiB. Sotto c'è la RAM mappata in identità, sopra l'heap del kernel.
 */
#define USER_SPACE_START 0x40000000
#define USER_SPACE_END   0xC0000000

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

/* ---- Spazi di indirizzi dei processi (vedi mm/paging.c) ----
 * Una page directory è una pagina con 1024 righe da 32 bit: la rappresentiamo con
 * un `uint32_t *`, il puntatore alla prima riga. */

/* La directory del kernel, usata dai thread del kernel. */
uint32_t *paging_get_kernel_directory(void);

/* Crea in anticipo le page table del kernel per la zona [start, start + size).
 * Va fatto PRIMA di creare la directory di qualsiasi processo. */
void paging_reserve_kernel_tables(uint32_t start, uint32_t size);

/* Crea la directory di un nuovo processo: kernel mappato, zona utente vuota.
 * Restituisce 0 se manca memoria. */
uint32_t *paging_create_directory(void);

/* Libera una directory creata con paging_create_directory, insieme a tutte le
 * pagine e le page table della sua zona utente. Non deve essere quella attiva. */
void paging_destroy_directory(uint32_t *page_directory);

/* Mappa una pagina nella zona utente della directory indicata (PAGE_USER viene
 * aggiunto da solo). Restituisce 0 se l'indirizzo è fuori zona o manca memoria. */
int paging_map_user_page(uint32_t *page_directory, uint32_t virtual_address,
                         uint32_t physical_address, uint32_t flags);

/* Rende attiva una directory (carica il registro CR3). */
void paging_switch_directory(uint32_t *page_directory);

/* Restituisce 1 se la zona [start, start + length) sta tutta nello spazio utente ed
 * è interamente mappata in quella directory. Serve a controllare i puntatori che i
 * programmi passano alle syscall. */
int paging_is_user_range(uint32_t *page_directory, uint32_t start, uint32_t length);

#endif /* MM_PAGING_H */
