/*
 * kernel/gdt.c — Costruzione della GDT (Global Descriptor Table).
 *
 * COS'È: in modalità protetta ogni accesso alla memoria passa da un "segmento".
 * La GDT è la tabella che descrive i segmenti: dove iniziano, quanto sono grandi,
 * e soprattutto CHI può usarli (kernel = livello di privilegio 0, utente = 3).
 *
 * COLLEGAMENTO CON LA TEORIA: è la "segmentazione" studiata a lezione. Noi però,
 * come Linux e Windows, la usiamo nel modo più banale possibile, il "modello piatto":
 * tutti i segmenti partono da 0 e coprono tutti i 4 GiB. Così indirizzo logico e
 * indirizzo lineare coincidono, e la protezione vera la farà la paginazione (fase 3).
 * La GDT resta comunque obbligatoria: la CPU x86 non funziona senza.
 *
 * PERCHÉ RIFARLA: il bootloader ne ha già impostata una, ma lo standard Multiboot
 * non dice DOVE si trova né come è fatta. Per gli interrupt ci serve sapere con
 * certezza quale selettore è il "codice del kernel", quindi ne creiamo una nostra.
 */

#include <stdint.h>
#include "kernel/gdt.h"

/*
 * Una riga della GDT ("descrittore di segmento"): 8 byte con un formato fissato da Intel.
 * La base (32 bit) e il limite (20 bit) sono spezzati in più campi per ragioni storiche
 * (compatibilità con il processore 286).
 *
 * `struct` = un gruppo di campi, come una classe Java con soli campi pubblici e nessun metodo.
 *
 * `__attribute__((packed))`: normalmente il compilatore può inserire byte vuoti
 * ("padding") tra i campi per allinearli. Qui la CPU si aspetta ESATTAMENTE questi
 * 8 byte in quest'ordine, quindi vietiamo il padding. In Java non hai mai il controllo
 * sulla disposizione dei campi in memoria; in C sì, e con l'hardware è indispensabile.
 */
struct gdt_entry {
    uint16_t limit_low;     /* limite, bit 0-15                                   */
    uint16_t base_low;      /* base, bit 0-15                                     */
    uint8_t  base_middle;   /* base, bit 16-23                                    */
    uint8_t  access;        /* tipo di segmento e livello di privilegio           */
    uint8_t  granularity;   /* 4 bit di flag (alti) + limite bit 16-19 (bassi)    */
    uint8_t  base_high;     /* base, bit 24-31                                    */
} __attribute__((packed));

/* Ciò che si passa all'istruzione `lgdt`: dimensione e indirizzo della tabella. */
struct gdt_pointer {
    uint16_t limit;         /* dimensione della tabella in byte, MENO 1 */
    uint32_t base;          /* indirizzo della prima riga               */
} __attribute__((packed));

/* Tre righe: nulla, codice kernel, dati kernel.
 * Nella fase 5 se ne aggiungeranno altre per la modalità utente. */
#define GDT_ENTRIES 3

static struct gdt_entry   gdt[GDT_ENTRIES];
static struct gdt_pointer gdt_ptr;

/* Definita in kernel/gdt_flush.asm: carica la GDT e ricarica i registri di segmento.
 * Serve l'assembly perché il C non ha modo di toccare i registri di segmento. */
extern void gdt_flush(uint32_t gdt_pointer_address);

/* Riempie la riga `index` della GDT spezzando base e limite nei vari campi. */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags)
{
    /* `& 0xFFFF` tiene i 16 bit bassi; `>> 16` porta in basso i bit dal 16 in su. */
    gdt[index].base_low    = (uint16_t) (base & 0xFFFF);
    gdt[index].base_middle = (uint8_t) ((base >> 16) & 0xFF);
    gdt[index].base_high   = (uint8_t) ((base >> 24) & 0xFF);

    gdt[index].limit_low   = (uint16_t) (limit & 0xFFFF);

    /* Byte misto: nei 4 bit bassi i bit 16-19 del limite, nei 4 bit alti i flag. */
    gdt[index].granularity = (uint8_t) (((limit >> 16) & 0x0F) | (flags & 0xF0));

    gdt[index].access      = access;
}

void gdt_init(void)
{
    /* Riga 0: la CPU pretende che la prima riga sia tutta a zero ("descrittore nullo").
     * Caricare il selettore 0 in un registro e poi usarlo provoca un'eccezione:
     * è una rete di sicurezza contro i registri non inizializzati. */
    gdt_set_entry(0, 0, 0, 0, 0);

    /*
     * Byte `access`, bit per bit (da sinistra):
     *   1     P    = presente
     *   00    DPL  = livello di privilegio 0 (kernel). Per l'utente sarà 11 (3).
     *   1     S    = segmento di codice/dati (non di sistema)
     *   1/0   E    = 1 eseguibile (codice), 0 dati
     *   0          = (direzione/conformità: non ci interessa)
     *   1     RW   = codice leggibile / dati scrivibili
     *   0     A    = "accesso avvenuto", lo imposta la CPU
     *
     *   codice: 1001 1010 = 0x9A        dati: 1001 0010 = 0x92
     *
     * Flag 0xC0 = 1100: G=1 (il limite si conta in pagine da 4 KiB, non in byte)
     *                   D=1 (segmento a 32 bit)
     * Limite 0xFFFFF pagine x 4 KiB = 4 GiB: il segmento copre tutta la memoria.
     */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC0);   /* 0x08: codice del kernel */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);   /* 0x10: dati del kernel   */

    /* `sizeof(gdt)` = dimensione in byte dell'intero array (3 x 8 = 24).
     * `&gdt` = l'indirizzo dell'array; il cast lo trasforma in un numero a 32 bit. */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t) &gdt;

    gdt_flush((uint32_t) &gdt_ptr);
}
