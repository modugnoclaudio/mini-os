/*
 * drivers/pic.c — Il PIC 8259 (Programmable Interrupt Controller).
 *
 * COS'È: la CPU ha UN SOLO filo per gli interrupt hardware. Il PIC è il chip che
 * raccoglie le richieste di tutti i dispositivi (16 linee, dette IRQ 0-15), sceglie
 * la più prioritaria e dice alla CPU quale numero di interrupt eseguire.
 *
 * Nel PC i PIC sono due, collegati "in cascata":
 *
 *    timer    --IRQ0--+
 *    tastiera --IRQ1--+  PIC      +----> CPU
 *                IRQ2-+  master --+
 *                 ^   ...
 *                 |
 *                 +---- PIC slave <-- IRQ 8-15 (orologio, mouse, dischi...)
 *
 * COLLEGAMENTO CON LA TEORIA: è il "controllore delle interruzioni" con priorità
 * e mascheramento visto a lezione.
 *
 * (I PC moderni usano un chip più evoluto, l'APIC, ma il vecchio PIC è ancora
 * emulato ovunque ed è molto più semplice.)
 */

#include <stdint.h>
#include "drivers/pic.h"
#include "drivers/io.h"

/* Ogni PIC ha due porte: una per i comandi e una per i dati. */
#define PIC1_COMMAND 0x20       /* master */
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0       /* slave  */
#define PIC2_DATA    0xA1

#define PIC_EOI      0x20       /* comando "End Of Interrupt" */

/* A quali numeri di interrupt far corrispondere gli IRQ. */
#define PIC1_OFFSET  32         /* IRQ 0-7  -> interrupt 32-39 */
#define PIC2_OFFSET  40         /* IRQ 8-15 -> interrupt 40-47 */

void pic_init(void)
{
    /*
     * PERCHÉ "RIMAPPARE": all'accensione il BIOS configura il PIC in modo che gli
     * IRQ 0-7 arrivino come interrupt 8-15. Ma in modalità protetta quei numeri sono
     * già le ECCEZIONI della CPU (8 = double fault, 13 = general protection fault...).
     * Un tick del timer sembrerebbe un double fault! Spostiamo quindi gli IRQ su 32-47,
     * subito dopo le 32 eccezioni.
     *
     * Il PIC si riprogramma con una sequenza fissa di 4 "parole di inizializzazione"
     * (ICW1..ICW4): la prima sulla porta comandi, le altre sulla porta dati.
     */

    /* ICW1: 0x11 = "inizia la configurazione, seguirà anche ICW4". */
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    /* ICW2: il numero di interrupt da cui parte ciascun PIC (è questo il rimappaggio). */
    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    /* ICW3: come sono collegati tra loro.
     * Al master: "lo slave è sulla mia linea 2" (maschera di bit: 0000 0100 = 4).
     * Allo slave: "sono collegato alla linea numero 2" (qui il numero, non la maschera). */
    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();

    /* ICW4: 0x01 = modalità 8086 (quella dei PC). */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /*
     * Maschera degli interrupt: un bit per linea, 1 = linea DISATTIVATA.
     * Partiamo con tutto spento; ogni driver accenderà la propria linea con
     * pic_enable_irq(). Unica eccezione la linea 2 del master (0xFB = 1111 1011):
     * è quella a cui è attaccato lo slave, senza di essa gli IRQ 8-15 non passano.
     */
    outb(PIC1_DATA, 0xFB);
    outb(PIC2_DATA, 0xFF);
}

void pic_enable_irq(uint8_t irq)
{
    uint16_t port;

    /* Gli IRQ 0-7 stanno sul master, gli 8-15 sullo slave (dove sono le linee 0-7). */
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq = irq - 8;
    }

    /* Leggiamo la maschera attuale, azzeriamo il bit della nostra linea, riscriviamo.
     *   1 << irq      = un numero con solo il bit `irq` a 1   (es. irq 1 -> 0000 0010)
     *   ~(...)        = tutti i bit invertiti                  (          -> 1111 1101)
     *   mask & ~(...) = la maschera di prima, con quel bit azzerato                      */
    uint8_t mask = inb(port);
    mask = (uint8_t) (mask & ~(1 << irq));
    outb(port, mask);
}

void pic_send_eoi(uint8_t irq)
{
    /*
     * Dopo aver consegnato un interrupt, il PIC non ne manda altri della stessa
     * priorità o inferiore finché non riceve la conferma "End Of Interrupt".
     * Dimenticarla è l'errore classico: arriva UN SOLO tick del timer e poi più nulla.
     *
     * Se l'interrupt veniva dallo slave (IRQ 8-15) vanno avvisati entrambi i chip,
     * perché è passato anche attraverso il master.
     */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}
