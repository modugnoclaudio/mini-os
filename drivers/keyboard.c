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
 * DAL TASTO AL PROGRAMMA: chi preme i tasti e chi li legge vanno a velocità diverse
 * e in momenti diversi. In mezzo ci vuole un deposito:
 *
 *    tasto --> interrupt --> keyboard_handler --> [ BUFFER ] --> keyboard_read_char --> processo
 *                            (produttore)                        (consumatore)
 *
 * COLLEGAMENTO CON LA TEORIA:
 *   - è l'I/O guidato dagli interrupt: nessuno controlla di continuo se c'è un tasto;
 *   - è il problema PRODUTTORE-CONSUMATORE con buffer limitato: il gestore
 *     dell'interrupt produce caratteri, un processo li consuma. Se il buffer è
 *     vuoto il consumatore si BLOCCA; il produttore lo risveglia;
 *   - il buffer è una coda FIFO realizzata come BUFFER CIRCOLARE.
 */

#include <stdint.h>
#include "drivers/keyboard.h"
#include "drivers/pic.h"
#include "drivers/io.h"
#include "kernel/interrupts.h"
#include "proc/process.h"
#include "proc/scheduler.h"

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

/* ---------------------------------------------------------------------------
 * Il buffer circolare dei caratteri
 * ---------------------------------------------------------------------------
 * Un array usato "ad anello": due indici si rincorrono, e arrivati in fondo
 * ripartono da zero (operazione `% BUFFER_SIZE`).
 *
 *        read_index             write_index
 *            v                       v
 *   [ . . . 'l' 's' ' ' '-' 'l' '\n' . . . . ]
 *            \______ count = 6 _____/
 *
 *   - si scrive in write_index, che poi avanza;
 *   - si legge da read_index, che poi avanza;
 *   - `count` dice quanti caratteri sono in attesa (0 = vuoto, BUFFER_SIZE = pieno).
 * È una coda FIFO: i caratteri escono nello stesso ordine in cui sono entrati.
 * In Java useresti una ArrayDeque o una BlockingQueue; qui la costruiamo a mano.
 */
#define BUFFER_SIZE 128

static char buffer[BUFFER_SIZE];
static uint32_t read_index = 0;
static uint32_t write_index = 0;
static uint32_t count = 0;

/* Il PID del processo bloccato in attesa di un tasto (0 = nessuno).
 * Teniamo il PID e non il puntatore al PCB: se nel frattempo il processo venisse
 * terminato e la sua memoria liberata, un puntatore resterebbe a indicare memoria
 * non più valida ("dangling pointer"), mentre un PID semplicemente non si trova più. */
static uint32_t waiting_pid = 0;

/*
 * Mette un carattere nel buffer e risveglia chi lo stava aspettando.
 * Va chiamata a interrupt disabilitati (lo sono sia nel gestore dell'interrupt
 * sia in keyboard_inject_char): il buffer è condiviso tra produttore e consumatore.
 */
static void buffer_put(char c)
{
    /* Buffer pieno: il carattere viene scartato. Capita solo se nessuno legge. */
    if (count == BUFFER_SIZE) {
        return;
    }

    buffer[write_index] = c;
    write_index = (write_index + 1) % BUFFER_SIZE;     /* dopo l'ultima casella si riparte da 0 */
    count++;

    /* Il "risveglio" del consumatore: da bloccato torna pronto. */
    if (waiting_pid != 0) {
        process_wake(process_find(waiting_pid));
        waiting_pid = 0;
    }
}

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

    /* Il gestore non stampa più nulla: deposita il carattere e torna subito.
     * A mostrarlo a schermo ("eco") penserà il programma che lo legge. */
    if (c != 0 && c != 27) {            /* 27 = Esc: non ha un carattere stampabile */
        buffer_put(c);
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

char keyboard_read_char(void)
{
    /*
     * SEZIONE CRITICA. "Il buffer è vuoto?" e "allora mi blocco" devono essere
     * un'unica operazione indivisibile. Se un tasto arrivasse proprio in mezzo
     * (dopo il controllo, prima del blocco), il gestore non troverebbe nessuno da
     * risvegliare, e il processo si addormenterebbe con un carattere già pronto:
     * resterebbe bloccato fino al tasto successivo. È il problema del "risveglio
     * perduto" (lost wakeup), un classico della programmazione concorrente.
     */
    uint32_t flags = interrupts_save_and_disable();

    /* `while` e non `if`: al risveglio la condizione va ricontrollata. */
    while (count == 0) {
        waiting_pid = current_process->pid;
        process_block();
        /* Qui si riprende quando buffer_put ci ha risvegliati. */
    }

    char c = buffer[read_index];
    read_index = (read_index + 1) % BUFFER_SIZE;
    count--;

    interrupts_restore(flags);
    return c;
}

int keyboard_buffer_is_empty(void)
{
    return count == 0;
}

void keyboard_inject_char(char c)
{
    uint32_t flags = interrupts_save_and_disable();
    buffer_put(c);
    interrupts_restore(flags);
}
