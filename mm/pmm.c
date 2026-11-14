/*
 * mm/pmm.c — Allocatore della memoria fisica con una bitmap.
 *
 * IL PROBLEMA: il kernel deve sapere quali pezzi di RAM sono liberi, per darli a chi
 * ne ha bisogno (tabelle delle pagine, heap, più avanti i processi) e riprenderli
 * quando non servono più.
 *
 * LA SOLUZIONE: dividiamo tutta la RAM in pagine fisiche ("frame") da 4 KiB e teniamo
 * una BITMAP: un array di bit, un bit per frame.
 *
 *      bit = 0  ->  frame libero            bit = 1  ->  frame occupato
 *
 *      frame:    0   1   2   3   4   5   6   7   8  ...
 *      bitmap:   1   1   1   0   0   1   0   0   0  ...
 *                            ^ il primo libero: è quello che pmm_alloc_page() restituisce
 *
 * COLLEGAMENTO CON LA TEORIA: è la "gestione dello spazio libero con bitmap" vista a
 * lezione (l'alternativa è la lista dei blocchi liberi). Costa pochissima memoria:
 * 1 bit ogni 4096 byte, cioè 32 KiB di bitmap per 1 GiB di RAM.
 *
 * PARAGONE CON JAVA: in Java scrivi `new` e la JVM trova la memoria; quando l'oggetto
 * non serve più ci pensa il garbage collector. Qui siamo sotto a tutto questo:
 * siamo NOI a decidere quale pezzo di RAM è di chi. E non c'è nessun garbage
 * collector: una pagina non restituita con pmm_free_page() è persa per sempre
 * ("memory leak").
 */

#include <stdint.h>
#include "mm/pmm.h"
#include "lib/kprintf.h"
#include "lib/string.h"

/* Numero massimo di frame: 1 GiB / 4 KiB = 262144. */
#define MAX_FRAMES (PMM_MAX_MEMORY / PAGE_SIZE)

/*
 * La bitmap: 262144 bit = 32768 byte (ogni uint8_t contiene 8 bit).
 * È un array globale di dimensione fissa, quindi sta nella sezione .bss del kernel:
 * non dobbiamo "allocarla" (sarebbe un bel problema: chi alloca la memoria per
 * l'allocatore?).
 */
static uint8_t bitmap[MAX_FRAMES / 8];

static uint32_t free_pages = 0;     /* quanti frame sono liberi adesso              */
static uint32_t memory_top = 0;     /* indirizzo dove finisce l'ultima zona di RAM  */

/* Simboli definiti in linker.ld: inizio e fine del kernel in memoria. */
extern char kernel_start[];
extern char kernel_end[];

/* ---------------------------------------------------------------------------
 * Operazioni sui singoli bit
 * ---------------------------------------------------------------------------
 * Il frame numero N corrisponde al bit numero (N % 8) del byte numero (N / 8).
 *   1 << (N % 8)   è un byte con acceso solo quel bit (una "maschera").
 */

static int frame_is_used(uint32_t frame)
{
    return bitmap[frame / 8] & (1 << (frame % 8));      /* AND: isola il bit */
}

static void frame_set_used(uint32_t frame)
{
    bitmap[frame / 8] |= (uint8_t) (1 << (frame % 8));  /* OR: accende il bit */
}

static void frame_set_free(uint32_t frame)
{
    bitmap[frame / 8] &= (uint8_t) ~(1 << (frame % 8)); /* AND con la maschera invertita: spegne il bit */
}

/* ---------------------------------------------------------------------------
 * Operazioni su intere zone di memoria
 * ---------------------------------------------------------------------------
 * `start` incluso, `end` escluso (come gli intervalli di substring() in Java).
 */

/* Segna come liberi i frame che stanno INTERAMENTE dentro la zona. */
static void mark_region_free(uint32_t start, uint32_t end)
{
    /* Arrotondiamo l'inizio per ECCESSO e la fine per DIFETTO al multiplo di 4096:
     * un frame che sta solo in parte nella zona libera non è utilizzabile.
     * (Divisione tra interi: tronca, come in Java.) */
    uint32_t first = (start + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t last  = end / PAGE_SIZE;

    for (uint32_t frame = first; frame < last; frame++) {
        if (frame_is_used(frame)) {
            frame_set_free(frame);
            free_pages++;
        }
    }
}

/* Segna come occupati i frame che toccano la zona, anche solo in parte. */
static void mark_region_used(uint32_t start, uint32_t end)
{
    /* Qui gli arrotondamenti sono al contrario: basta un byte occupato perché
     * l'intero frame non si possa dare a nessuno. */
    uint32_t first = start / PAGE_SIZE;
    uint32_t last  = (end + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t frame = first; frame < last && frame < MAX_FRAMES; frame++) {
        if (!frame_is_used(frame)) {
            frame_set_used(frame);
            free_pages--;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Inizializzazione
 * --------------------------------------------------------------------------- */

void pmm_init(struct multiboot_info *mbi)
{
    /* Strategia prudente: partiamo con TUTTO occupato (tutti i bit a 1, 0xFF = 1111 1111)
     * e liberiamo solo ciò che il bootloader dichiara esplicitamente come RAM libera.
     * Così i buchi e le zone sconosciute restano intoccabili. */
    memset(bitmap, 0xFF, sizeof(bitmap));
    free_pages = 0;

    if (!(mbi->flags & MULTIBOOT_FLAG_MMAP)) {
        kprintf("PMM: ERRORE, il bootloader non ha fornito la mappa della memoria\n");
        return;
    }

    kprintf("Mappa della memoria fornita dal bootloader:\n");

    /*
     * Scorriamo le voci della mappa. NON sono un normale array: ogni voce dichiara
     * la propria dimensione nel campo `size`, quindi non si può scrivere entry[i].
     * Teniamo invece un "offset" in byte dall'inizio della mappa e lo facciamo
     * avanzare di volta in volta.
     */
    uint32_t offset = 0;
    while (offset < mbi->mmap_length) {

        /* Aritmetica sugli indirizzi: (inizio della mappa + offset) è un numero;
         * il cast lo trasforma in un puntatore a una voce, per leggerne i campi. */
        struct multiboot_mmap_entry *entry =
            (struct multiboot_mmap_entry *) (mbi->mmap_addr + offset);

        /* Prossima voce: `size` non conta se stesso, quindi si aggiungono i suoi 4 byte. */
        offset += entry->size + 4;

        /* Zone che iniziano oltre i 4 GiB: irraggiungibili per un kernel a 32 bit. */
        if (entry->addr_high != 0) {
            continue;
        }

        uint32_t start = entry->addr_low;
        uint32_t end   = entry->addr_low + entry->len_low;

        /* Se la zona supera i 4 GiB la somma qui sopra "fa il giro" e dà un numero
         * più piccolo di `start` (per gli interi senza segno il giro è un
         * comportamento definito). In quel caso, e in ogni caso oltre il nostro
         * limite di 1 GiB, tronchiamo. */
        if (entry->len_high != 0 || end < start || end > PMM_MAX_MEMORY) {
            end = PMM_MAX_MEMORY;
        }

        int available = (entry->type == MULTIBOOT_MEMORY_AVAILABLE);

        kprintf("  da 0x%x per %d KiB: %s\n",
                start, entry->len_low / 1024, available ? "libera" : "riservata");

        if (available && start < end) {
            mark_region_free(start, end);

            if (end > memory_top) {
                memory_top = end;
            }
        }
    }

    /*
     * Ora togliamo dalle zone libere ciò che in realtà è già in uso.
     *
     * 1) Tutto il primo MiB. Sulla carta in parte è RAM libera, ma lì vivono i dati
     *    del BIOS, la memoria video (0xB8000) e la struttura Multiboot che stiamo
     *    leggendo proprio ora. Rinunciare a 1 MiB è il modo più semplice di non
     *    sbagliare. Così anche il frame 0 non viene mai distribuito, e l'indirizzo 0
     *    resta libero di significare "nessuna pagina".
     */
    mark_region_used(0, 0x100000);

    /* 2) Il kernel stesso: codice, dati, stack e questa bitmap. */
    mark_region_used((uint32_t) kernel_start, (uint32_t) kernel_end);

    kprintf("PMM: %d pagine libere (%d MiB), la RAM finisce a 0x%x\n",
            free_pages, free_pages / 256, memory_top);    /* 256 pagine da 4 KiB = 1 MiB */
}

/* ---------------------------------------------------------------------------
 * Allocazione e rilascio
 * --------------------------------------------------------------------------- */

uint32_t pmm_alloc_page(void)
{
    uint32_t frame_count = memory_top / PAGE_SIZE;

    /* Ricerca "first fit": il primo frame libero che si trova, partendo dal basso.
     * È una ricerca lineare, lenta se la memoria è molto piena, ma semplicissima. */
    for (uint32_t frame = 0; frame < frame_count; frame++) {

        /* Scorciatoia: se tutti gli 8 bit del byte sono a 1 (0xFF) lì non c'è nulla
         * di libero, e si saltano 8 frame in un colpo solo. */
        if (frame % 8 == 0 && bitmap[frame / 8] == 0xFF) {
            frame += 7;     /* +7 qui, +1 dal for = 8 */
            continue;
        }

        if (!frame_is_used(frame)) {
            frame_set_used(frame);
            free_pages--;
            return frame * PAGE_SIZE;   /* da numero di frame a indirizzo fisico */
        }
    }

    return 0;   /* memoria esaurita */
}

void pmm_free_page(uint32_t address)
{
    uint32_t frame = address / PAGE_SIZE;

    /* Controlli contro gli errori di chi chiama. In Java certi errori sono
     * impossibili per costruzione; in C dobbiamo difenderci a mano. */
    if (address % PAGE_SIZE != 0 || frame == 0 || frame >= MAX_FRAMES) {
        kprintf("PMM: ERRORE, pmm_free_page(0x%x): indirizzo non valido\n", address);
        return;
    }

    /* "Double free": liberare due volte la stessa pagina. È un bug serio: la pagina
     * potrebbe essere stata nel frattempo assegnata a qualcun altro. */
    if (!frame_is_used(frame)) {
        kprintf("PMM: ERRORE, la pagina 0x%x era gia' libera (double free)\n", address);
        return;
    }

    frame_set_free(frame);
    free_pages++;
}

uint32_t pmm_get_free_pages(void)
{
    return free_pages;
}

uint32_t pmm_get_memory_top(void)
{
    return memory_top;
}
