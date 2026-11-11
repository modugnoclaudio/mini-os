/*
 * drivers/keyboard.c — La tastiera PS/2.
 *
 * COME FUNZIONA: a ogni tasto premuto O rilasciato, il controllore della tastiera
 * genera un interrupt sull'IRQ 1 e mette a disposizione sulla porta 0x60 un numero,
 * lo "scancode", che identifica il TASTO FISICO (non il carattere):
 *
 *     pressione di un tasto  = scancode          (es. 'A' = 0x1E)
 *     rilascio dello stesso  = scancode + 0x80   (es. 'A' = 0x9E), cioè col bit 7 acceso
 *
 * Tradurre il tasto in un carattere è compito nostro, con una tabella. È per questo
 * che esistono i "layout di tastiera": stessi scancode, tabelle diverse.
 *
 * COLLEGAMENTO CON LA TEORIA: è l'esempio classico di I/O guidato dagli interrupt.
 * Il kernel non controlla continuamente se c'è un tasto (polling): dorme, ed è il
 * dispositivo ad avvisarlo. Confronta con l'attesa attiva di drivers/serial.c.
 */

#include <stdint.h>
#include "drivers/keyboard.h"
#include "drivers/pic.h"
#include "drivers/io.h"
#include "kernel/interrupts.h"
#include "lib/kprintf.h"

#define KEYBOARD_DATA_PORT 0x60

/* Scancode che ci interessano in modo particolare. */
#define SCANCODE_LEFT_SHIFT   0x2A
#define SCANCODE_RIGHT_SHIFT  0x36
#define SCANCODE_EXTENDED     0xE0      /* prefisso dei tasti "estesi" (frecce, Canc...) */
#define SCANCODE_RELEASE_BIT  0x80      /* bit 7: acceso = tasto rilasciato              */

/*
 * Tabelle di traduzione scancode -> carattere, layout di tastiera AMERICANO (US).
 * L'indice dell'array è lo scancode. 0 significa "tasto senza carattere" (Ctrl, F1...).
 *
 * Sintassi: un array di char si può inizializzare elencando i valori. Gli elementi
 * non elencati (qui quelli dopo il 57) valgono automaticamente 0.
 * '\b' = backspace, '\t' = tabulazione, '\n' = invio.
 */
static const char scancode_normal[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',  /* 0-14  */
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',      /* 15-28 */
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',            /* 29-41 */
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,              /* 42-54 */
    '*', 0,   ' '                                                                /* 55-57 */
};

/* La stessa tabella con Shift premuto. */
static const char scancode_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',  /* 0-14  */
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',      /* 15-28 */
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',             /* 29-41 */
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,               /* 42-54 */
    '*', 0,   ' '                                                                /* 55-57 */
};

/* Stato che deve sopravvivere tra un interrupt e l'altro: per questo sono variabili
 * globali (`static` = private di questo file) e non locali del gestore. */
static int shift_pressed = 0;       /* 1 mentre uno dei due Shift è tenuto premuto      */
static int extended_pending = 0;    /* 1 se l'ultimo byte ricevuto era il prefisso 0xE0 */

/* Gestore dell'IRQ 1: eseguito a ogni pressione e a ogni rilascio di un tasto. */
static void keyboard_handler(struct registers *regs)
{
    (void) regs;    /* parametro non usato */

    /* Lo scancode va SEMPRE letto, anche se poi lo ignoriamo: finché non lo leggiamo
     * il controllore non manda l'interrupt del tasto successivo. */
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /* Tasti estesi (frecce, Ins, Canc...): arrivano come DUE byte, 0xE0 e poi il codice.
     * Per ora li ignoriamo entrambi, altrimenti il secondo byte verrebbe scambiato
     * per un tasto normale. */
    if (scancode == SCANCODE_EXTENDED) {
        extended_pending = 1;
        return;
    }
    if (extended_pending) {
        extended_pending = 0;
        return;
    }

    /* Separiamo "quale tasto" (7 bit bassi) da "premuto o rilasciato" (bit 7). */
    int released = scancode & SCANCODE_RELEASE_BIT;
    uint8_t key  = scancode & 0x7F;

    /* Shift: ci interessa sia la pressione sia il rilascio, per sapere se è tenuto giù. */
    if (key == SCANCODE_LEFT_SHIFT || key == SCANCODE_RIGHT_SHIFT) {
        shift_pressed = !released;      /* `!` trasforma 0 in 1 e qualsiasi altro valore in 0 */
        return;
    }

    /* Per tutti gli altri tasti conta solo la pressione. */
    if (released) {
        return;
    }

    char c = shift_pressed ? scancode_shift[key] : scancode_normal[key];

    /* Per ora il carattere viene semplicemente mostrato ("eco").
     * Nella fase 6 finirà in un buffer da cui la shell leggerà i comandi. */
    if (c != 0 && c != 27) {            /* 27 = Esc: non ha un carattere stampabile */
        kputchar(c);
    }
}

void keyboard_init(void)
{
    interrupt_register_handler(INT_IRQ_BASE + IRQ_KEYBOARD, keyboard_handler);

    /* Se un tasto è stato premuto prima di adesso (es. nel menu del BIOS), il suo
     * scancode è ancora lì e bloccherebbe i successivi: lo leggiamo e lo buttiamo. */
    inb(KEYBOARD_DATA_PORT);

    pic_enable_irq(IRQ_KEYBOARD);
}
