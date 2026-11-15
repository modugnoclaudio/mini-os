/*
 * fs/fstest.c — Prove automatiche del filesystem in RAM.
 *
 * Stesso schema delle altre prove (mm/memtest.c, proc/proctest.c): ogni verifica
 * stampa "ok" oppure "ERRORE".
 */

#include <stdint.h>
#include "fs/fstest.h"
#include "fs/ramfs.h"
#include "mm/kheap.h"
#include "lib/kprintf.h"
#include "lib/string.h"

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

/* Restituisce 1 se i primi `count` byte delle due zone di memoria sono uguali. */
static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

void fstest_run(void)
{
    kprintf("Prova del filesystem in RAM:\n");

    /* Un buffer per le letture. È un array locale: vive sullo stack. */
    uint8_t buffer[64];

    /* --- I file iniziali --- */
    check("i file iniziali esistono",
          fs_find("benvenuto.txt") != 0 && fs_find("leggimi.txt") != 0 && fs_find("hello") != 0);
    check("un file inesistente non viene trovato", fs_find("non-esiste") == 0);

    /* Lettura dell'inizio di un file. Il cast (const uint8_t *) serve perché una
     * stringa letterale è un array di `char`, mentre qui confrontiamo byte. */
    int32_t read = fs_read("benvenuto.txt", 0, buffer, 9);
    check("lettura dell'inizio di un file",
          read == 9 && bytes_equal(buffer, (const uint8_t *) "Benvenuto", 9));

    /* Lettura a partire da una posizione intermedia (offset). */
    read = fs_read("benvenuto.txt", 10, buffer, 2);
    check("lettura da una posizione intermedia",
          read == 2 && bytes_equal(buffer, (const uint8_t *) "in", 2));

    /* Lettura a cavallo della fine: si ottengono solo i byte che esistono. */
    uint32_t size = fs_find("benvenuto.txt")->size;
    check("vicino alla fine si leggono solo i byte rimasti",
          fs_read("benvenuto.txt", size - 3, buffer, sizeof(buffer)) == 3);
    check("oltre la fine non si legge nulla",
          fs_read("benvenuto.txt", size, buffer, sizeof(buffer)) == 0 &&
          fs_read("benvenuto.txt", size + 1000, buffer, sizeof(buffer)) == 0);
    check("leggere un file inesistente restituisce -1",
          fs_read("non-esiste", 0, buffer, sizeof(buffer)) == -1);

    /* --- Creazione, sostituzione, cancellazione --- */
    uint32_t count_before = fs_count();
    uint32_t heap_before = kheap_get_used_bytes();

    check("creazione di un file nuovo",
          fs_write("prova.txt", (const uint8_t *) "ciao", 4));
    check("il file nuovo compare nell'elenco", fs_count() == count_before + 1);
    read = fs_read("prova.txt", 0, buffer, sizeof(buffer));
    check("il contenuto e' quello scritto",
          read == 4 && bytes_equal(buffer, (const uint8_t *) "ciao", 4));

    check("riscrivere un file ne sostituisce il contenuto",
          fs_write("prova.txt", (const uint8_t *) "un testo piu' lungo", 19) &&
          fs_find("prova.txt")->size == 19 && fs_count() == count_before + 1);

    check("si puo' creare un file vuoto",
          fs_write("vuoto", 0, 0) && fs_find("vuoto")->size == 0 &&
          fs_read("vuoto", 0, buffer, sizeof(buffer)) == 0);

    check("cancellazione dei file", fs_delete("prova.txt") && fs_delete("vuoto"));
    check("un file cancellato non esiste piu'",
          fs_find("prova.txt") == 0 && fs_count() == count_before);
    check("cancellare un file inesistente fallisce", !fs_delete("prova.txt"));
    check("nessun memory leak: l'heap e' tornato come prima",
          kheap_get_used_bytes() == heap_before);

    /* --- Richieste non valide --- */
    check("un nome vuoto viene rifiutato", !fs_write("", (const uint8_t *) "x", 1));
    check("un nome con spazi viene rifiutato",
          !fs_write("due parole", (const uint8_t *) "x", 1));
    check("un nome troppo lungo viene rifiutato",
          !fs_write("questo-nome-e-decisamente-troppo-lungo.txt", (const uint8_t *) "x", 1));
    check("un file troppo grande viene rifiutato",
          !fs_write("enorme", buffer, FS_MAX_FILE_SIZE + 1));

    /* --- Riempimento della tabella --- */
    /* Creiamo file finché c'è posto. I nomi sono "pieno-AA", "pieno-AB"...:
     * modifichiamo due caratteri di un array di char (una stringa C è modificabile
     * se è un array, non se è una stringa letterale). */
    char name[] = "pieno-AA";
    uint32_t created = 0;

    for (uint32_t i = 0; i < FS_MAX_FILES + 5; i++) {
        name[6] = (char) ('A' + i / 26);
        name[7] = (char) ('A' + i % 26);
        if (!fs_write(name, (const uint8_t *) "x", 1)) {
            break;
        }
        created++;
    }
    check("la tabella si riempie esattamente fino al limite",
          created == FS_MAX_FILES - count_before && fs_count() == FS_MAX_FILES);

    /* E li cancelliamo tutti, per lasciare il filesystem com'era. */
    for (uint32_t i = 0; i < created; i++) {
        name[6] = (char) ('A' + i / 26);
        name[7] = (char) ('A' + i % 26);
        fs_delete(name);
    }
    check("dopo la pulizia il filesystem e' tornato com'era",
          fs_count() == count_before && kheap_get_used_bytes() == heap_before);

    if (failures == 0) {
        kprintf("File system: tutte le prove superate\n");
    } else {
        kprintf("File system: %d prove FALLITE\n", failures);
    }
}
