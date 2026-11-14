/*
 * mm/memtest.c — Prove automatiche della gestione della memoria.
 *
 * Un kernel non ha JUnit. Ma l'idea è la stessa: eseguire un'operazione, confrontare
 * il risultato con quello atteso, segnalare le differenze. Qui lo facciamo all'avvio,
 * stampando l'esito; `make test` controlla poi che non ci siano errori.
 */

#include <stdint.h>
#include "mm/memtest.h"
#include "mm/pmm.h"
#include "mm/paging.h"
#include "mm/kheap.h"
#include "lib/string.h"
#include "lib/kprintf.h"

/* Quante verifiche sono fallite finora. */
static int failures = 0;

/* Controlla una condizione e ne stampa l'esito (l'equivalente di assertTrue). */
static void check(const char *description, int condition)
{
    if (condition) {
        kprintf("  ok: %s\n", description);
    } else {
        kprintf("  ERRORE: %s\n", description);
        failures++;
    }
}

void memtest_pmm(void)
{
    kprintf("Prova dell'allocatore fisico:\n");

    uint32_t free_before = pmm_get_free_pages();

    uint32_t a = pmm_alloc_page();
    uint32_t b = pmm_alloc_page();
    kprintf("  pagine ottenute: 0x%x e 0x%x\n", a, b);

    check("le due pagine esistono e sono diverse", a != 0 && b != 0 && a != b);
    check("gli indirizzi sono multipli di 4096", a % PAGE_SIZE == 0 && b % PAGE_SIZE == 0);
    check("le pagine stanno dopo il primo MiB", a >= 0x100000 && b >= 0x100000);
    check("il contatore e' sceso di 2", pmm_get_free_pages() == free_before - 2);

    /* Liberiamo la prima: essendo la più bassa, la prossima richiesta deve riottenerla. */
    pmm_free_page(a);
    uint32_t c = pmm_alloc_page();
    check("una pagina liberata viene riutilizzata", c == a);

    pmm_free_page(b);
    pmm_free_page(c);
    check("dopo aver liberato tutto il contatore torna com'era",
          pmm_get_free_pages() == free_before);
}

void memtest_paging(void)
{
    kprintf("Prova della paginazione:\n");

    /* Un indirizzo virtuale qualsiasi, lontano da tutto ciò che è già mappato. */
    uint32_t virtual_address = 0xE0000000;

    uint32_t frame = pmm_alloc_page();
    check("mappatura di una nuova pagina virtuale",
          frame != 0 && paging_map_page(virtual_address, frame, PAGE_WRITABLE));

    /*
     * Ora la STESSA pagina fisica è raggiungibile da DUE indirizzi virtuali:
     *   - 0xE0000000, la mappatura appena creata;
     *   - `frame`, grazie alla mappatura in identità di tutta la RAM.
     * Scriviamo da uno e leggiamo dall'altro: se il valore coincide, la traduzione
     * degli indirizzi sta funzionando davvero.
     */
    volatile uint32_t *through_new_mapping = (volatile uint32_t *) virtual_address;
    volatile uint32_t *through_identity    = (volatile uint32_t *) frame;

    *through_new_mapping = 0xCAFE1234;
    kprintf("  scritto 0x%x a 0x%x, letto 0x%x a 0x%x\n",
            0xCAFE1234, virtual_address, *through_identity, frame);
    check("due indirizzi virtuali vedono la stessa pagina fisica",
          *through_identity == 0xCAFE1234);

    check("la traduzione fatta a mano restituisce la pagina giusta",
          paging_get_physical(virtual_address + 0x10) == frame + 0x10);
    check("un indirizzo del kernel e' mappato in identita'",
          paging_get_physical(0x100000) == 0x100000);
    check("la pagina 0 non e' mappata", paging_get_physical(0) == 0);

    paging_unmap_page(virtual_address);
    check("dopo l'unmap la pagina non e' piu' raggiungibile",
          paging_get_physical(virtual_address) == 0);

    pmm_free_page(frame);
}

/* Restituisce 1 se tutti i `count` byte a partire da `pointer` valgono `value`. */
static int all_bytes_equal(const uint8_t *pointer, uint32_t count, uint8_t value)
{
    for (uint32_t i = 0; i < count; i++) {
        if (pointer[i] != value) {
            return 0;
        }
    }
    return 1;
}

void memtest_heap(void)
{
    kprintf("Prova dell'heap:\n");

    /* kmalloc restituisce un `void *` (indirizzo senza tipo): il cast dice come
     * vogliamo usare quella memoria. Qui come array di byte. */
    uint8_t *a = (uint8_t *) kmalloc(40);
    uint8_t *b = (uint8_t *) kmalloc(200);
    uint8_t *c = (uint8_t *) kmalloc(1000);

    check("tre allocazioni riuscite", a != 0 && b != 0 && c != 0);
    check("gli indirizzi sono nella zona dell'heap e multipli di 8",
          (uint32_t) a >= KHEAP_START && (uint32_t) a % 8 == 0 &&
          (uint32_t) b % 8 == 0 && (uint32_t) c % 8 == 0);

    /* Riempiamo ogni blocco con un valore diverso e poi rileggiamo tutto:
     * se due blocchi si sovrapponessero, uno dei tre risulterebbe sporcato. */
    memset(a, 0xAA, 40);
    memset(b, 0xBB, 200);
    memset(c, 0xCC, 1000);
    check("i tre blocchi non si sovrappongono",
          all_bytes_equal(a, 40, 0xAA) && all_bytes_equal(b, 200, 0xBB) &&
          all_bytes_equal(c, 1000, 0xCC));

    kheap_dump();

    /* Liberando il blocco in mezzo resta un "buco" di 200 byte. Una richiesta più
     * piccola deve finire lì (first fit), non in fondo all'heap. */
    kfree(b);
    uint8_t *d = (uint8_t *) kmalloc(100);
    check("un blocco liberato viene riutilizzato (first fit)", d == b);

    /* Un blocco più grande di una pagina: l'heap deve crescere da solo. */
    uint32_t big_size = 3 * PAGE_SIZE;
    uint8_t *e = (uint8_t *) kmalloc(big_size);
    check("allocazione grande (12 KiB): l'heap cresce", e != 0);
    if (e != 0) {
        e[0] = 1;
        e[big_size - 1] = 2;        /* l'ultimo byte: dev'essere mappato anche lui */
        check("il blocco grande e' scrivibile fino in fondo",
              e[0] == 1 && e[big_size - 1] == 2);
    }

    /* Liberando tutto, i blocchi liberi vicini devono fondersi fino a tornare uno solo. */
    kfree(a);
    kfree(c);
    kfree(d);
    kfree(e);
    check("dopo aver liberato tutto non resta nulla di occupato",
          kheap_get_used_bytes() == 0);
    check("i blocchi liberi si sono fusi in uno solo", kheap_get_block_count() == 1);

    kheap_dump();
}

void memtest_summary(void)
{
    if (failures == 0) {
        kprintf("Memoria: tutte le prove superate\n");
    } else {
        kprintf("Memoria: %d prove FALLITE\n", failures);
    }
}
