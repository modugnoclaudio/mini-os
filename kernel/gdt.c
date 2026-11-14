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
#include "lib/string.h"

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

/*
 * Il TSS (Task State Segment). Intel lo aveva pensato per far fare il context switch
 * direttamente alla CPU; nessun sistema operativo moderno lo usa così. Ne resta però
 * obbligatorio UN uso, e solo due campi ci interessano: `ss0` ed `esp0`.
 *
 * IL PROBLEMA CHE RISOLVE: quando arriva un interrupt (o una syscall) mentre gira un
 * programma utente, la CPU deve salvare i registri su uno stack. Non può fidarsi di
 * quello dell'utente: potrebbe essere pieno, o puntare apposta dentro al kernel.
 * Allora PRIMA cambia stack, e l'indirizzo di quello nuovo, sicuro, lo legge da qui:
 * `ss0:esp0` = "stack da usare quando si entra nel livello di privilegio 0".
 *
 * COLLEGAMENTO CON LA TEORIA: è il motivo per cui ogni processo ha DUE stack, uno
 * per la modalità utente e uno per la modalità kernel.
 *
 * Tutti gli altri campi restano a zero: vanno dichiarati solo perché la struttura
 * abbia la dimensione e la forma che la CPU si aspetta.
 */
struct tss_entry {
    uint32_t previous_tss;
    uint32_t esp0;          /* cima dello stack del kernel del processo corrente */
    uint32_t ss0;           /* segmento di quello stack: i dati del kernel       */
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;    /* dove inizia la mappa dei permessi sulle porte di I/O */
} __attribute__((packed));

/* Sei righe: nulla, codice kernel, dati kernel, codice utente, dati utente, TSS. */
#define GDT_ENTRIES 6

static struct tss_entry tss;

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

    /*
     * I segmenti per i programmi utente sono identici a quelli del kernel (stessa
     * base, stesso limite) tranne che per il livello di privilegio, DPL = 11 (3):
     *
     *   codice: 1111 1010 = 0xFA        dati: 1111 0010 = 0xF2
     *
     * È il registro CS a dire alla CPU "in che modalità siamo": con CS = segmento
     * utente le istruzioni privilegiate (cli, hlt, in/out, caricare CR3...) sono
     * vietate e provocano un'eccezione.
     * COLLEGAMENTO CON LA TEORIA: sono i due "modi" della CPU, kernel e utente
     * (su x86 si chiamano ring 0 e ring 3).
     */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xC0);   /* 0x18: codice utente */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);   /* 0x20: dati utente   */

    /*
     * Il TSS: prima lo azzeriamo, poi riempiamo i soli campi che usiamo.
     * `esp0` verrà impostato dallo scheduler a ogni cambio di processo.
     * `iomap_base` oltre la fine della struttura = "nessuna mappa": i programmi
     * utente non possono usare nessuna porta di I/O.
     */
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = GDT_KERNEL_DATA;
    tss.iomap_base = sizeof(tss);

    /* La riga della GDT che descrive il TSS: qui base e limite contano davvero
     * (indirizzo e dimensione della struttura). 0x89 = presente, tipo "TSS a 32 bit". */
    gdt_set_entry(5, (uint32_t) &tss, sizeof(tss) - 1, 0x89, 0x00);

    /* `sizeof(gdt)` = dimensione in byte dell'intero array (6 x 8 = 48).
     * `&gdt` = l'indirizzo dell'array; il cast lo trasforma in un numero a 32 bit. */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t) &gdt;

    gdt_flush((uint32_t) &gdt_ptr);

    /* `ltr` ("load task register") dice alla CPU quale riga della GDT contiene il TSS.
     * Vuole il selettore in un registro a 16 bit. */
    uint16_t tss_selector = GDT_TSS;
    __asm__ volatile ("ltr %0" : : "r"(tss_selector));
}

void gdt_set_kernel_stack(uint32_t stack_top)
{
    tss.esp0 = stack_top;
}
