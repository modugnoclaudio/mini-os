/*
 * drivers/timer.c — Il timer di sistema (PIT, Programmable Interval Timer).
 *
 * COS'È: un chip con un contatore che scende al ritmo di un oscillatore a
 * 1 193 182 Hz. Ogni volta che arriva a zero genera un interrupt sull'IRQ 0 e
 * ricomincia. Scegliendo il valore di partenza decidiamo la frequenza degli interrupt.
 *
 * COLLEGAMENTO CON LA TEORIA: è l'"interrupt di clock". È il cuore del
 * multitasking preemptive: grazie a lui il kernel riprende periodicamente il
 * controllo della CPU anche se un processo non la cede mai. Nella fase 4 lo
 * scheduler round-robin verrà chiamato proprio da qui, e il "quanto di tempo"
 * sarà un certo numero di tick.
 */

#include <stdint.h>
#include "drivers/timer.h"
#include "drivers/pic.h"
#include "drivers/io.h"
#include "kernel/interrupts.h"

#define PIT_CHANNEL0     0x40       /* porta dati del canale 0 (quello collegato all'IRQ 0) */
#define PIT_COMMAND      0x43       /* porta comandi                                        */
#define PIT_FREQUENCY    1193182    /* frequenza dell'oscillatore, in Hz                    */

/*
 * Contatore dei tick.
 *
 * `volatile` qui ha un motivo diverso dalla memoria video, ma la stessa logica:
 * la variabile viene modificata dal gestore dell'interrupt, cioè "alle spalle" del
 * codice normale. Senza `volatile`, in un ciclo come `while (ticks < 100) {}` il
 * compilatore potrebbe leggere `ticks` una volta sola e concludere che non cambia mai.
 * In Java la parola chiave `volatile` esiste e serve a qualcosa di simile:
 * una variabile condivisa tra thread che va sempre riletta dalla memoria.
 */
static volatile uint32_t ticks = 0;

/* Gestore dell'IRQ 0: eseguito TIMER_HZ volte al secondo.
 * Il parametro non ci serve; il cast a (void) evita l'avviso "parametro non usato".
 * REGOLA D'ORO dei gestori di interrupt: fare pochissimo e tornare subito. */
static void timer_handler(struct registers *regs)
{
    (void) regs;
    ticks++;
}

void timer_init(void)
{
    /* Valore di partenza del contatore: 1193182 / 100 = 11931.
     * Deve stare in 16 bit (massimo 65535), quindi la frequenza minima è circa 18 Hz. */
    uint32_t divisor = PIT_FREQUENCY / TIMER_HZ;

    /* Prima registriamo il gestore, POI attiviamo la linea: così non può arrivare
     * un interrupt che nessuno è pronto a gestire. */
    interrupt_register_handler(INT_IRQ_BASE + IRQ_TIMER, timer_handler);

    /* Comando 0x36 = 0011 0110:
     *   00   canale 0
     *   11   il divisore verrà inviato in due parti: prima byte basso, poi byte alto
     *   011  modalità 3 (onda quadra): ricomincia da solo a ogni giro
     *   0    conteggio binario                                                    */
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t) (divisor & 0xFF));         /* byte basso */
    outb(PIT_CHANNEL0, (uint8_t) ((divisor >> 8) & 0xFF));  /* byte alto  */

    pic_enable_irq(IRQ_TIMER);
}

uint32_t timer_get_ticks(void)
{
    /* Leggere una variabile a 32 bit su x86 è una singola istruzione, quindi non può
     * essere interrotta "a metà" dal gestore. Con un contatore a 64 bit servirebbe
     * disabilitare gli interrupt durante la lettura (sezione critica). */
    return ticks;
}
