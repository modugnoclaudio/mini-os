/*
 * linker.ld — Lo "script del linker": dice a `ld` come disporre il kernel in memoria.
 *
 * In Java non esiste niente di simile: la JVM decide da sola dove mettere classi e oggetti.
 * Qui invece non c'è nessuno sotto di noi, quindi dobbiamo decidere NOI a quale
 * indirizzo fisico sta ogni pezzo del programma.
 *
 * Il compilatore produce, per ogni file .c/.asm, dei file oggetto (.o) divisi in "sezioni":
 *   .text    = il codice macchina (le funzioni)
 *   .rodata  = dati in sola lettura (es. le stringhe "Hello, kernel!")
 *   .data    = variabili globali con un valore iniziale
 *   .bss     = variabili globali senza valore iniziale (valgono 0)
 * Il linker unisce le sezioni omonime di tutti i .o e le mette dove diciamo qui.
 */

/* Il punto di ingresso: la prima istruzione eseguita. È l'etichetta _start in boot/boot.asm. */
ENTRY(_start)

SECTIONS
{
    /*
     * "." è il "contatore di posizione": l'indirizzo a cui stiamo scrivendo.
     * Partiamo da 1 MiB (0x100000). Sotto 1 MiB ci sono il BIOS, la memoria video
     * (0xB8000) e altre zone riservate dell'hardware PC: meglio starne fuori.
     * 1 MiB è la scelta classica per i kernel Multiboot.
     */
    . = 1M;

    /* Simbolo che segna l'inizio del kernel: dal C si legge con `extern char kernel_start[];` */
    kernel_start = .;

    /*
     * L'header Multiboot DEVE stare nei primi 8 KiB del file, altrimenti il bootloader
     * non lo trova. Per questo gli diamo una sezione tutta sua e la mettiamo per prima.
     * KEEP(...) vieta al linker di scartarla anche se nessuno "la usa" dal codice.
     * ALIGN(4K) allinea ogni sezione a multipli di 4096 byte (la dimensione di una pagina:
     * ci tornerà comodo nella fase 3 con la paginazione).
     */
    .multiboot ALIGN(4K) :
    {
        KEEP(*(.multiboot))
    }

    /* Il codice. *(.text) significa "la sezione .text di tutti i file oggetto". */
    .text ALIGN(4K) :
    {
        *(.text .text.*)
    }

    /* Dati in sola lettura: costanti e stringhe letterali. */
    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    /* Variabili globali inizializzate. */
    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    /*
     * Variabili globali non inizializzate e lo stack del kernel (definito in boot.asm).
     * Nel file ELF la .bss non occupa spazio: il bootloader la riserva e la azzera in RAM.
     * COMMON è un vecchio modo in cui il C chiama alcune globali non inizializzate.
     */
    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /* Simbolo che segna la fine del kernel: servirà all'allocatore di memoria (fase 3). */
    kernel_end = .;

    /* Sezioni che non ci servono nel kernel: le buttiamo via. */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.gnu.property)
    }
}
