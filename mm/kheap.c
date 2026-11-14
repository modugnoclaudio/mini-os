/*
 * mm/kheap.c — L'heap del kernel: allocazione dinamica di blocchi di qualsiasi dimensione.
 *
 * IL PROBLEMA: l'allocatore fisico (mm/pmm.c) distribuisce solo pagine intere da
 * 4096 byte. Ma al kernel servono continuamente oggetti piccoli e di dimensione
 * varia: 40 byte per la descrizione di un processo, 200 per un nome di file...
 * Sprecare una pagina per ciascuno sarebbe assurdo.
 *
 * LA SOLUZIONE: prendiamo delle pagine, le mettiamo una dopo l'altra in una zona
 * di memoria virtuale (l'heap) e le ritagliamo in blocchi. Davanti a ogni blocco
 * c'è una piccola intestazione ("header") con la sua dimensione e se è libero:
 *
 *   KHEAP_START                                                        heap_end
 *   |                                                                         |
 *   v                                                                         v
 *   +--------+-----------+--------+---------------+--------+------------------+
 *   | header |   dati    | header |     dati      | header |      dati        |
 *   | occup. |  (40 b.)  | LIBERO |   (200 b.)    | occup. |    (3800 b.)     |
 *   +--------+-----------+--------+---------------+--------+------------------+
 *            ^
 *            puntatore restituito da kmalloc: punta ai dati, subito DOPO l'header
 *
 * Gli header sono collegati in una lista (ognuno punta al successivo).
 *
 * COLLEGAMENTO CON LA TEORIA: è l'allocazione dinamica a partizioni variabili.
 *   - "first fit": si usa il primo blocco libero abbastanza grande;
 *   - "frammentazione esterna": tanti buchi liberi piccoli e sparsi, che sommati
 *     basterebbero ma singolarmente no. Si combatte fondendo i blocchi liberi vicini;
 *   - "frammentazione interna": lo spazio sprecato per arrotondamenti e header.
 *
 * PARAGONE CON JAVA: è ciò che sta sotto a `new`. Ma qui non c'è garbage collector:
 * ogni kmalloc deve avere il suo kfree, altrimenti la memoria è persa (memory leak).
 * E usare un blocco dopo averlo liberato ("use after free") non dà errore: rovina
 * in silenzio i dati di qualcun altro. Sono i bug più temuti del C.
 */

#include <stdint.h>
#include "mm/kheap.h"
#include "mm/pmm.h"
#include "mm/paging.h"
#include "lib/kprintf.h"

/* Numero "magico" scritto in ogni header: se non lo ritroviamo, vuol dire che il
 * puntatore non viene da kmalloc oppure che qualcuno ha scritto fuori dal proprio
 * blocco e ha rovinato l'header. Un valore qualsiasi, purché improbabile. */
#define BLOCK_MAGIC 0xB10CCA75

/* Tutte le dimensioni vengono arrotondate a multipli di 8 byte: così ogni blocco
 * inizia a un indirizzo "allineato", come si aspettano compilatore e CPU. */
#define ALIGNMENT 8

/*
 * L'intestazione di ogni blocco: 16 byte.
 *
 * Nota il campo `next`: una struct che contiene un puntatore a un'altra struct
 * dello stesso tipo. È esattamente una lista concatenata, come
 * `class Nodo { Nodo next; }` in Java, solo che i nodi non sono oggetti creati
 * con `new`: stanno DENTRO la memoria che stiamo gestendo.
 */
struct block_header {
    uint32_t magic;                 /* sempre BLOCK_MAGIC                              */
    uint32_t size;                  /* dimensione dei DATI in byte (header escluso)    */
    uint32_t is_free;               /* 1 = libero, 0 = occupato                        */
    struct block_header *next;      /* blocco successivo, 0 se è l'ultimo              */
};

static struct block_header *first_block = 0;    /* inizio della lista (0 = heap vuoto) */
static uint32_t heap_end = KHEAP_START;         /* primo indirizzo NON ancora mappato   */

/* Arrotonda per eccesso al multiplo di 8. Es.: 13 -> 16, 16 -> 16. */
static uint32_t align_up(uint32_t value)
{
    return (value + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
}

/*
 * Fonde i blocchi liberi adiacenti in un unico blocco più grande.
 * È il rimedio alla frammentazione esterna: senza, dopo molti kmalloc/kfree
 * l'heap si ridurrebbe a briciole inutilizzabili.
 *
 * I blocchi nella lista sono sempre in ordine di indirizzo e senza buchi tra loro,
 * quindi "successivo nella lista" significa anche "subito dopo in memoria".
 */
static void merge_free_blocks(void)
{
    struct block_header *block = first_block;

    while (block != 0 && block->next != 0) {
        if (block->is_free && block->next->is_free) {
            /* Il blocco assorbe il successivo: guadagna i suoi dati E il suo header. */
            block->size += sizeof(struct block_header) + block->next->size;
            block->next = block->next->next;
            /* Non avanziamo: il nuovo successivo potrebbe essere libero anche lui. */
        } else {
            block = block->next;
        }
    }
}

/*
 * Fa crescere l'heap di almeno `bytes` byte: chiede pagine fisiche al PMM e le
 * mappa una dopo l'altra in fondo all'heap. Restituisce 1 se riesce, 0 se no.
 *
 * Qui si vede a cosa serve la paginazione: le pagine fisiche ottenute possono
 * essere sparse ovunque nella RAM, ma nello spazio virtuale appaiono contigue.
 */
static int heap_grow(uint32_t bytes)
{
    uint32_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;   /* arrotondato per eccesso */
    uint32_t grow_size = pages * PAGE_SIZE;

    /* Limite massimo dell'heap. */
    if (heap_end - KHEAP_START + grow_size > KHEAP_MAX_SIZE) {
        return 0;
    }

    /* Controlliamo PRIMA che ci siano abbastanza pagine (più una di scorta per
     * un'eventuale nuova page table), per non fermarci a metà dell'opera. */
    if (pmm_get_free_pages() < pages + 1) {
        return 0;
    }

    uint32_t region_start = heap_end;

    for (uint32_t i = 0; i < pages; i++) {
        uint32_t frame = pmm_alloc_page();
        if (frame == 0 || !paging_map_page(heap_end, frame, PAGE_WRITABLE)) {
            return 0;
        }
        heap_end += PAGE_SIZE;
    }

    /* La zona appena mappata diventa un unico grande blocco libero.
     * Il cast trasforma il suo indirizzo iniziale in un puntatore a header:
     * "da qui in poi, questi 16 byte sono l'intestazione di un blocco". */
    struct block_header *new_block = (struct block_header *) region_start;
    new_block->magic   = BLOCK_MAGIC;
    new_block->size    = grow_size - sizeof(struct block_header);
    new_block->is_free = 1;
    new_block->next    = 0;

    /* Lo agganciamo in fondo alla lista. */
    if (first_block == 0) {
        first_block = new_block;
    } else {
        struct block_header *last = first_block;
        while (last->next != 0) {
            last = last->next;
        }
        last->next = new_block;
    }

    /* Se l'ultimo blocco di prima era libero, si fonde con quello nuovo. */
    merge_free_blocks();
    return 1;
}

/* Cerca il primo blocco libero con almeno `size` byte di dati ("first fit"). */
static struct block_header *find_free_block(uint32_t size)
{
    for (struct block_header *block = first_block; block != 0; block = block->next) {
        if (block->is_free && block->size >= size) {
            return block;
        }
    }
    return 0;
}

/*
 * Se il blocco è più grande del necessario, lo taglia in due: la prima parte
 * (di `size` byte) verrà usata, il resto diventa un nuovo blocco libero.
 *
 *   prima:  | header |              dati (size grande)              |
 *   dopo:   | header | dati (size) | header nuovo |  dati rimasti   |
 */
static void split_block(struct block_header *block, uint32_t size)
{
    /* Si taglia solo se nel resto ci stanno un header e almeno 8 byte di dati:
     * altrimenti il pezzo avanzato sarebbe inutilizzabile, tanto vale lasciarlo
     * al blocco (un po' di frammentazione interna). */
    if (block->size < size + sizeof(struct block_header) + ALIGNMENT) {
        return;
    }

    /*
     * Indirizzo del nuovo header: inizio del blocco + un header + `size` byte di dati.
     *
     * ATTENZIONE all'aritmetica dei puntatori: sommare 1 a un `struct block_header *`
     * lo sposta di un header INTERO (16 byte), non di 1 byte. Per contare in byte
     * bisogna prima convertire a `uint8_t *` (puntatore a byte).
     */
    struct block_header *rest =
        (struct block_header *) ((uint8_t *) block + sizeof(struct block_header) + size);

    rest->magic   = BLOCK_MAGIC;
    rest->size    = block->size - size - sizeof(struct block_header);
    rest->is_free = 1;
    rest->next    = block->next;

    block->size = size;
    block->next = rest;
}

void kheap_init(void)
{
    first_block = 0;
    heap_end = KHEAP_START;
    kprintf("Heap del kernel a 0x%x (massimo %d MiB), cresce a richiesta\n",
            KHEAP_START, KHEAP_MAX_SIZE / (1024 * 1024));
}

void *kmalloc(uint32_t size)
{
    if (size == 0) {
        return 0;
    }

    size = align_up(size);

    struct block_header *block = find_free_block(size);

    /* Nessun blocco adatto: facciamo crescere l'heap e riproviamo. */
    if (block == 0) {
        if (!heap_grow(size + sizeof(struct block_header))) {
            return 0;       /* memoria esaurita */
        }
        block = find_free_block(size);
        if (block == 0) {
            return 0;
        }
    }

    split_block(block, size);
    block->is_free = 0;

    /* `block + 1` = l'indirizzo subito dopo l'header (vedi nota sull'aritmetica dei
     * puntatori in split_block): è lì che iniziano i dati da consegnare.
     * Il cast a `void *` dice: "indirizzo generico, deciderà chi chiama cosa metterci". */
    return (void *) (block + 1);
}

void kfree(void *pointer)
{
    if (pointer == 0) {
        return;
    }

    /* Percorso inverso di kmalloc: dall'indirizzo dei dati torniamo indietro di un
     * header per ritrovare l'intestazione del blocco. */
    struct block_header *block = (struct block_header *) pointer - 1;

    if (block->magic != BLOCK_MAGIC) {
        kprintf("HEAP: ERRORE, kfree(0x%x): non e' un blocco valido (o l'header e' rovinato)\n",
                (uint32_t) pointer);
        return;
    }

    if (block->is_free) {
        kprintf("HEAP: ERRORE, kfree(0x%x): blocco gia' liberato (double free)\n",
                (uint32_t) pointer);
        return;
    }

    block->is_free = 1;
    merge_free_blocks();
}

void kheap_dump(void)
{
    kprintf("  heap da 0x%x a 0x%x:\n", KHEAP_START, heap_end);

    for (struct block_header *block = first_block; block != 0; block = block->next) {
        kprintf("    blocco a 0x%x: %d byte, %s\n",
                (uint32_t) (block + 1), block->size,
                block->is_free ? "libero" : "occupato");
    }
}

uint32_t kheap_get_used_bytes(void)
{
    uint32_t total = 0;

    for (struct block_header *block = first_block; block != 0; block = block->next) {
        if (!block->is_free) {
            total += block->size;
        }
    }
    return total;
}

uint32_t kheap_get_block_count(void)
{
    uint32_t count = 0;

    for (struct block_header *block = first_block; block != 0; block = block->next) {
        count++;
    }
    return count;
}
