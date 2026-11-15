/*
 * kernel/multiboot.h — Le strutture che il bootloader ci lascia in memoria.
 *
 * Prima di saltare al kernel, il bootloader Multiboot prepara una struttura con le
 * informazioni sulla macchina e ne mette l'indirizzo nel registro EBX
 * (boot/boot.asm lo passa a kernel_main). A noi interessa una cosa sola:
 * la MAPPA DELLA MEMORIA, cioè l'elenco delle zone di RAM usabili e di quelle riservate.
 *
 * Il formato è fissato dallo standard Multiboot 1: i campi vanno dichiarati
 * esattamente in quest'ordine e con queste dimensioni.
 */
#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

/* Il campo `flags` dice quali altri campi sono validi: un bit per ciascuno.
 * Il bit 6 riguarda la mappa della memoria. */
#define MULTIBOOT_FLAG_MMAP    (1 << 6)

/* Il bit 2 riguarda la "riga di comando": un testo che chi avvia il kernel può
 * passargli (con QEMU: l'opzione -append). */
#define MULTIBOOT_FLAG_CMDLINE (1 << 2)

/* Tipo di una zona di memoria: 1 = RAM libera. Qualsiasi altro valore = non toccare
 * (riservata al BIOS, tabelle ACPI, memoria difettosa...). */
#define MULTIBOOT_MEMORY_AVAILABLE 1

/* La struttura principale. Ha molti altri campi dopo `mmap_addr`, ma non ci servono:
 * basta dichiarare la parte iniziale, fino all'ultimo campo che leggiamo. */
struct multiboot_info {
    uint32_t flags;         /* quali campi sono validi                              */
    uint32_t mem_lower;     /* KiB di memoria sotto 1 MiB                           */
    uint32_t mem_upper;     /* KiB di memoria sopra 1 MiB                           */
    uint32_t boot_device;
    uint32_t cmdline;       /* indirizzo fisico della riga di comando (una stringa) */
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;   /* dimensione TOTALE in byte della mappa della memoria  */
    uint32_t mmap_addr;     /* indirizzo fisico della prima voce della mappa        */
} __attribute__((packed));

/*
 * Una voce della mappa della memoria: "da questo indirizzo, per questa lunghezza,
 * la memoria è di questo tipo".
 *
 * Indirizzo e lunghezza sono numeri a 64 bit (la mappa può descrivere più di 4 GiB).
 * Noi siamo a 32 bit, quindi li dichiariamo come due metà da 32 bit ciascuno:
 * x86 è "little endian", cioè in memoria viene prima la metà bassa e poi quella alta.
 * Così evitiamo l'aritmetica a 64 bit, che su una CPU a 32 bit richiede funzioni di
 * supporto che non abbiamo.
 *
 * ATTENZIONE: il campo `size` contiene la dimensione della voce SENZA contare
 * se stesso. La voce successiva si trova quindi `size + 4` byte più avanti.
 */
struct multiboot_mmap_entry {
    uint32_t size;
    uint32_t addr_low;      /* indirizzo di inizio, 32 bit bassi                */
    uint32_t addr_high;     /* indirizzo di inizio, 32 bit alti (0 sotto 4 GiB) */
    uint32_t len_low;       /* lunghezza in byte, 32 bit bassi                  */
    uint32_t len_high;      /* lunghezza in byte, 32 bit alti                   */
    uint32_t type;          /* 1 = libera, altro = riservata                    */
} __attribute__((packed));

#endif /* KERNEL_MULTIBOOT_H */
