/*
 * kernel/kernel.c — Il punto di ingresso in C del kernel.
 *
 * Qui arriva l'esecuzione dopo boot/boot.asm, che ha preparato lo stack.
 * È l'equivalente del `main` di Java, con due differenze importanti:
 *   - non c'è nessuna JVM né sistema operativo sotto di noi;
 *   - non deve MAI ritornare (non c'è nessuno a cui tornare).
 *
 * kernel_main fa tre cose, in quest'ordine:
 *   1. inizializza i pezzi del kernel, dal più basso al più alto (ognuno ha
 *      bisogno di quelli prima);
 *   2. avvia il primo programma utente, la shell;
 *   3. diventa il processo "idle", che gira quando nessun altro ha da fare.
 *
 * DUE MODI DI AVVIO:
 *   - normale (`make run`): si arriva direttamente alla shell;
 *   - di collaudo (`make test`): se la riga di comando del kernel contiene la
 *     parola "autotest", a ogni passo vengono eseguite anche le prove automatiche,
 *     e alla fine un processo collauda la shell e spegne la macchina virtuale.
 */

#include <stdint.h>
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "lib/kprintf.h"
#include "lib/string.h"
#include "kernel/gdt.h"
#include "kernel/interrupts.h"
#include "kernel/syscall.h"
#include "kernel/multiboot.h"
#include "kernel/autotest.h"
#include "mm/pmm.h"
#include "mm/paging.h"
#include "mm/kheap.h"
#include "mm/memtest.h"
#include "proc/process.h"
#include "proc/scheduler.h"
#include "proc/programs.h"
#include "proc/proctest.h"
#include "fs/ramfs.h"
#include "fs/fstest.h"

/* Il valore che un bootloader Multiboot mette in EAX prima di saltare al kernel. */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Il nome del file che contiene il primo programma da avviare. */
#define SHELL_PROGRAM "shell"

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
 * Il kernel è stato avviato in modalità di collaudo?
 * Chi avvia un kernel può passargli una "riga di comando", come gli argomenti di un
 * programma (con QEMU: l'opzione -append). Il bootloader ce ne lascia l'indirizzo
 * nella struttura Multiboot. Linux fa lo stesso: si può leggere in /proc/cmdline.
 */
static int autotest_requested(struct multiboot_info *mbi)
{
    if (!(mbi->flags & MULTIBOOT_FLAG_CMDLINE)) {
        return 0;
    }

    /* Il campo contiene un indirizzo: il cast lo trasforma in un puntatore a stringa. */
    const char *command_line = (const char *) mbi->cmdline;
    return string_contains(command_line, "autotest");
}

/* Avvia la shell come processo utente. Restituisce 1 se ci riesce. */
static int start_shell(void)
{
    if (program_run(SHELL_PROGRAM) == 0) {
        kprintf("ERRORE: impossibile avviare il programma '%s'\n", SHELL_PROGRAM);
        return 0;
    }
    return 1;
}

/*
 * Chiamata da _start in boot/boot.asm con due argomenti messi sullo stack:
 *   magic          = numero magico Multiboot (era nel registro EAX)
 *   multiboot_info = indirizzo fisico della struttura con le info di sistema (era in EBX).
 *                    Arriva come semplice numero: più sotto lo convertiamo in un
 *                    puntatore per leggere la mappa della memoria.
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

    /* Il cast dice al compilatore: "a questo indirizzo c'è una struct multiboot_info".
     * Non copia nulla: cambia solo il modo in cui leggiamo quei byte. */
    struct multiboot_info *mbi = (struct multiboot_info *) multiboot_info;

    /* Va letto subito: più avanti quella zona di memoria non sarà più garantita. */
    int autotest = autotest_requested(mbi);
    if (autotest) {
        kprintf("Modalita' di collaudo: verranno eseguite le prove automatiche\n");
    }

    /* Il cast (uint32_t) trasforma un indirizzo (puntatore) in un numero stampabile.
     * Su x86 a 32 bit un puntatore occupa esattamente 32 bit, quindi non si perde nulla. */
    uint32_t start = (uint32_t) kernel_start;
    uint32_t end   = (uint32_t) kernel_end;
    kprintf("Kernel caricato da 0x%x a 0x%x (%d KiB)\n", start, end, (end - start) / 1024);

    if (autotest) {
        /* Piccola dimostrazione di tutti i formati di kprintf. */
        kprintf("Prova kprintf: %s, %d, %d, 0x%x, %c, 100%%\n",
                "stringa", 42, -7, 255, 'Z');
    }

    /* --- Tabelle della CPU, interrupt e syscall (fasi 2 e 5) --- */
    gdt_init();
    interrupts_init();
    syscall_init();
    kprintf("GDT, IDT e syscall pronte; PIC rimappato\n");

    if (autotest) {
        /* `int3` genera apposta l'eccezione 3. Se IDT, stub e ritorno con `iret`
         * funzionano, il gestore stampa un messaggio e l'esecuzione prosegue qui sotto. */
        __asm__ volatile ("int3");
        kprintf("Ritorno dall'eccezione riuscito\n");
    }

    /* --- Memoria (fase 3) --- */
    pmm_init(mbi);
    if (autotest) {
        memtest_pmm();
    }

    paging_init();
    if (autotest) {
        memtest_paging();
    }

    kheap_init();
    if (autotest) {
        memtest_heap();
        memtest_address_spaces();
        memtest_summary();
    }

    /* --- Filesystem (fase 6) --- */
    fs_init();
    if (autotest) {
        fstest_run();
    }

    /* --- Dispositivi (fase 2) --- */
    timer_init();
    keyboard_init();
    kprintf("Timer a %d Hz e tastiera pronti\n", TIMER_HZ);

    /* Da qui in poi la CPU accetta gli interrupt hardware. Va fatto per ultimo,
     * quando tabelle e gestori sono tutti al loro posto. */
    interrupts_enable();

    /* --- Processi (fasi 4 e 5) --- */

    /* Da questo momento il codice che stiamo eseguendo È il processo 0. */
    process_init();
    kprintf("Processi pronti: kernel_main e' ora il processo 0 (idle)\n");

    if (autotest) {
        proctest_cooperative();
        proctest_preemptive();
        proctest_syscalls();
        proctest_user_programs();
        proctest_keyboard();
        proctest_summary();
    }

    /*
     * Il primo programma utente. Su Unix si chiama "init" ed è l'antenato di tutti
     * gli altri processi; da noi è direttamente la shell.
     */
    kprintf("Avvio della shell...\n");
    start_shell();

    if (autotest) {
        autotest_start();
    }

    /*
     * Ciclo del processo idle. `hlt` addormenta la CPU fino al prossimo interrupt:
     * niente attesa attiva. Questo codice gira solo quando nessun altro processo è
     * pronto (lo scheduler sceglie il processo 0 per ultimo): tipicamente mentre
     * la shell aspetta che l'utente prema un tasto.
     * A ogni risveglio fa due lavoretti di manutenzione:
     *   - libera la memoria dei processi terminati;
     *   - se la shell non esiste più (l'utente ha scritto `exit`, o è stata
     *     terminata per un errore) ne avvia un'altra: un sistema senza shell
     *     non sarebbe più utilizzabile.
     */
    for (;;) {
        __asm__ volatile ("hlt");

        process_reap_terminated();

        /* La ricerca nella lista dei processi va fatta a interrupt spenti. */
        uint32_t flags = interrupts_save_and_disable();
        int shell_exists = process_find_by_name(SHELL_PROGRAM) != 0;
        interrupts_restore(flags);

        if (!shell_exists) {
            /* Se nemmeno questo riesce non resta nulla da fare: ci si ferma, invece
             * di riprovare all'infinito riempiendo lo schermo di errori. */
            if (!start_shell()) {
                halt();
            }
        }
    }
}
