/*
 * user/user.ld — Script del linker per i PROGRAMMI UTENTE.
 *
 * I programmi utente non fanno parte del kernel: vengono compilati e collegati a
 * parte, ognuno in un proprio file, e pensati per girare a un indirizzo VIRTUALE
 * fisso, 0x40000000. Tutti i programmi usano lo stesso indirizzo: non si pestano i
 * piedi perché ogni processo ha il suo spazio di indirizzi (vedi mm/paging.c).
 *
 * Il risultato è un file "binario piatto": solo i byte di codice e dati, uno dopo
 * l'altro, senza intestazioni. Il kernel lo carica copiandolo così com'è a
 * 0x40000000 e saltando al primo byte. (I sistemi veri usano formati più ricchi,
 * come ELF, che descrivono sezioni, permessi e punto di ingresso.)
 */

ENTRY(_start)

SECTIONS
{
    /* Deve coincidere con USER_CODE_BASE in proc/process.h. */
    . = 0x40000000;

    .text :
    {
        /* Il codice di avvio (user/lib/crt0.asm) va per PRIMO: il kernel inizia
         * l'esecuzione dal primo byte del file, e lì deve trovare _start. */
        *(.text.start)
        *(.text .text.*)
    }

    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }

    /* Le variabili non inizializzate: non occupano spazio nel file. Il kernel
     * consegna al programma pagine già azzerate, quindi valgono 0 come devono. */
    .bss :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    program_end = .;

    /* Il kernel riserva 64 KiB a ogni programma (USER_IMAGE_PAGES in
     * proc/process.h): meglio un errore qui che un crash misterioso in esecuzione. */
    ASSERT(program_end <= 0x40000000 + 0x10000, "Programma troppo grande: massimo 64 KiB")

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}
