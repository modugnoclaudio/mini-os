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

void memtest_summary(void)
{
    if (failures == 0) {
        kprintf("Memoria: tutte le prove superate\n");
    } else {
        kprintf("Memoria: %d prove FALLITE\n", failures);
    }
}
