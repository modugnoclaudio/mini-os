/*
 * kernel/idt.c — Costruzione della IDT (Interrupt Descriptor Table).
 *
 * COS'È: una tabella di 256 righe. Quando arriva l'interrupt numero N, la CPU legge
 * la riga N e salta all'indirizzo che ci trova.
 *
 * COLLEGAMENTO CON LA TEORIA: è il "vettore delle interruzioni" studiato a lezione.
 *
 * PARAGONE CON JAVA: assomiglia a una mappa di listener/callback
 * (numero dell'evento -> codice da eseguire), ma a consultarla è direttamente
 * l'hardware, non una libreria.
 */

#include <stdint.h>
#include "kernel/idt.h"
#include "kernel/gdt.h"

/* Una riga della IDT ("gate"): 8 byte, formato fissato da Intel.
 * L'indirizzo del gestore è spezzato in due metà per ragioni storiche.
 * `packed`: nessun byte di riempimento tra i campi (vedi kernel/gdt.c). */
struct idt_entry {
    uint16_t offset_low;    /* indirizzo del gestore, bit 0-15              */
    uint16_t selector;      /* segmento di codice in cui si trova il gestore */
    uint8_t  zero;          /* sempre 0                                      */
    uint8_t  type_attr;     /* tipo di gate e privilegi                      */
    uint16_t offset_high;   /* indirizzo del gestore, bit 16-31             */
} __attribute__((packed));

/* Ciò che si passa all'istruzione `lidt`: dimensione e indirizzo della tabella. */
struct idt_pointer {
    uint16_t limit;         /* dimensione in byte, meno 1 */
    uint32_t base;          /* indirizzo della prima riga */
} __attribute__((packed));

#define IDT_ENTRIES 256

/*
 * Tipo di gate 0x8E = 1000 1110:
 *   1     presente
 *   00    privilegio richiesto per invocarlo con `int`: 0 = solo il kernel
 *   0     (sempre 0)
 *   1110  "interrupt gate" a 32 bit: all'ingresso la CPU DISABILITA gli interrupt,
 *         e `iret` li riabilita. Così un gestore non viene interrotto da un altro:
 *         niente interrupt annidati, molto più semplice da ragionare.
 */
#define IDT_INTERRUPT_GATE 0x8E

/* Le variabili globali `static` non inizializzate partono a zero (stanno nella .bss):
 * tutte le 256 righe nascono quindi "non presenti". */
static struct idt_entry   idt[IDT_ENTRIES];
static struct idt_pointer idt_ptr;

/* Definita in kernel/interrupt_stubs.asm: gli indirizzi dei 48 stub in assembly. */
extern uint32_t interrupt_stub_table[INTERRUPT_STUB_COUNT];

/* Scrive nella riga `number` l'indirizzo `handler`. */
static void idt_set_gate(int number, uint32_t handler)
{
    idt[number].offset_low  = (uint16_t) (handler & 0xFFFF);
    idt[number].offset_high = (uint16_t) ((handler >> 16) & 0xFFFF);
    idt[number].selector    = GDT_KERNEL_CODE;
    idt[number].zero        = 0;
    idt[number].type_attr   = IDT_INTERRUPT_GATE;
}

void idt_init(void)
{
    /* Le prime 48 righe puntano ai nostri stub. Le altre restano a zero ("non
     * presente"): se arrivasse uno di quegli interrupt, la CPU genererebbe
     * un'eccezione 13 (general protection fault), che noi gestiamo. */
    for (int i = 0; i < INTERRUPT_STUB_COUNT; i++) {
        idt_set_gate(i, interrupt_stub_table[i]);
    }

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t) &idt;

    /* `lidt` dice alla CPU dove si trova la IDT.
     * "m"(idt_ptr) = passa la variabile come operando in memoria; %0 è il suo segnaposto. */
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}
