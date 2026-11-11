/*
 * kernel/kernel.c — Il punto di ingresso in C del kernel.
 *
 * Qui arriva l'esecuzione dopo boot/boot.asm, che ha preparato lo stack.
 * È l'equivalente del `main` di Java, con due differenze importanti:
 *   - non c'è nessuna JVM né sistema operativo sotto di noi;
 *   - non deve MAI ritornare (non c'è nessuno a cui tornare).
 */

#include <stdint.h>
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "lib/kprintf.h"

/* Il valore che un bootloader Multiboot mette in EAX prima di saltare al kernel. */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/*
 * Simboli definiti in linker.ld (non in un file C): segnano inizio e fine del kernel.
 * `extern` = "esiste da qualche altra parte, la troverà il linker".
 * Non hanno un contenuto utile: ci interessa solo il loro INDIRIZZO. Dichiararli come
 * array di char permette di scrivere semplicemente `kernel_start` per ottenerlo
 * (in C il nome di un array, usato in un'espressione, vale l'indirizzo del suo primo elemento).
 */
extern char kernel_start[];
extern char kernel_end[];

/*
 * Ferma la CPU per sempre.
 * `hlt` mette la CPU a riposo fino al prossimo interrupt; il ciclo la riferma se
 * si risveglia. Senza questo, la CPU continuerebbe a eseguire i byte che trova in
 * memoria dopo la funzione, come se fossero istruzioni.
 */
static void halt(void)
{
    for (;;) {                      /* for (;;) = ciclo infinito, come while (true) */
        __asm__ volatile ("hlt");
    }
}

/*
 * Chiamata da _start in boot/boot.asm con due argomenti messi sullo stack:
 *   magic          = numero magico Multiboot (era nel registro EAX)
 *   multiboot_info = indirizzo fisico della struttura con le info di sistema (era in EBX).
 *                    Per ora lo teniamo come semplice numero; lo useremo nella fase 3
 *                    per leggere la mappa della memoria.
 */
void kernel_main(uint32_t magic, uint32_t multiboot_info)
{
    /* I driver vanno inizializzati PRIMA di qualsiasi kprintf. */
    serial_init();
    vga_init();

    kprintf("Hello, kernel!\n");

    /* Controllo di sanità: siamo stati davvero caricati da un bootloader Multiboot? */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kprintf("ERRORE: magic Multiboot errato: 0x%x (atteso 0x%x)\n",
                magic, MULTIBOOT_BOOTLOADER_MAGIC);
        halt();
    }

    kprintf("Multiboot: magic 0x%x, info a 0x%x\n", magic, multiboot_info);

    /* Il cast (uint32_t) trasforma un indirizzo (puntatore) in un numero stampabile.
     * Su x86 a 32 bit un puntatore occupa esattamente 32 bit, quindi non si perde nulla. */
    uint32_t start = (uint32_t) kernel_start;
    uint32_t end   = (uint32_t) kernel_end;
    kprintf("Kernel caricato da 0x%x a 0x%x (%d byte)\n", start, end, end - start);

    /* Piccola dimostrazione di tutti i formati di kprintf. */
    kprintf("Prova kprintf: %s, %d, %d, 0x%x, %c, 100%%\n",
            "stringa", 42, -7, 255, 'Z');

    kprintf("Kernel fermo (hlt). Fine della fase 1.\n");
    halt();
}
