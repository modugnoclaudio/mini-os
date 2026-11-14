/*
 * mm/kheap.h — L'heap del kernel: kmalloc e kfree.
 * Il codice è in mm/kheap.c.
 */
#ifndef MM_KHEAP_H
#define MM_KHEAP_H

#include <stdint.h>

/* Dove vive l'heap nello spazio di indirizzi VIRTUALE, e quanto può crescere al massimo. */
#define KHEAP_START    0xD0000000
#define KHEAP_MAX_SIZE 0x01000000      /* 16 MiB */

/* Prepara l'heap (all'inizio è vuoto: cresce alla prima richiesta). */
void kheap_init(void);

/*
 * Riserva `size` byte e restituisce un puntatore al primo.
 * Restituisce 0 (puntatore nullo) se non c'è memoria: va SEMPRE controllato.
 * Il contenuto non è azzerato. L'indirizzo restituito è multiplo di 8.
 *
 * È il `new` di Java, con due differenze: non sa nulla del tipo (restituisce un
 * `void *`, sta a chi chiama farne il cast), e la memoria va restituita a mano.
 */
void *kmalloc(uint32_t size);

/* Restituisce all'heap un blocco ottenuto con kmalloc. Dopo la chiamata il puntatore
 * non va più usato. kfree(0) non fa nulla. */
void kfree(void *pointer);

/* Stampa l'elenco dei blocchi dell'heap, liberi e occupati (per capire e per il debug). */
void kheap_dump(void);

/* Statistiche: byte occupati dai blocchi in uso, e numero di blocchi in totale. */
uint32_t kheap_get_used_bytes(void);
uint32_t kheap_get_block_count(void);

#endif /* MM_KHEAP_H */
