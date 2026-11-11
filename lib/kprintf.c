/*
 * lib/kprintf.c — Una printf minimale per il kernel.
 *
 * PERCHÉ SCRIVERLA NOI: la `printf` vera sta nella libreria standard del C, che a sua
 * volta chiede al sistema operativo di scrivere (syscall write). Ma qui il sistema
 * operativo siamo noi: sotto non c'è nessuno. Quindi niente libreria standard
 * (opzioni -ffreestanding -nostdlib) e tutto ciò che serve lo scriviamo a mano.
 */

/* stdarg.h, come stdint.h, è fornito dal compilatore e si può usare in un kernel.
 * Contiene gli strumenti per leggere gli argomenti variabili (`...`). */
#include <stdarg.h>
#include <stdint.h>
#include "lib/kprintf.h"
#include "drivers/vga.h"
#include "drivers/serial.h"

/* Ogni carattere del kernel passa da qui e va su ENTRAMBE le uscite. */
void kputchar(char c)
{
    vga_putchar(c);
    serial_putchar(c);
}

/*
 * Stampa una stringa.
 *
 * In C non esiste il tipo String: una stringa è un array di `char` che termina con
 * il carattere speciale '\0' (un byte che vale zero). `const char *s` è un puntatore
 * al primo carattere; `const` significa "attraverso s non modifico i caratteri".
 * Non c'è un campo .length(): si scorre finché non si incontra lo zero finale.
 */
static void print_string(const char *s)
{
    /* Un puntatore nullo in Java darebbe NullPointerException; qui leggerebbe
     * l'indirizzo 0 con risultati imprevedibili. Meglio controllare. */
    if (s == 0) {
        s = "(null)";
    }

    while (*s != '\0') {    /* `*s` = "il carattere a cui punta s" (dereferenziazione) */
        kputchar(*s);
        s++;                /* sposta il puntatore al carattere successivo */
    }
}

/*
 * Stampa un numero senza segno nella base indicata (10 oppure 16).
 *
 * Algoritmo: `value % base` dà l'ultima cifra, `value / base` toglie l'ultima cifra.
 * Le cifre escono quindi al CONTRARIO (dalla meno significativa): le salviamo in un
 * piccolo array e poi le stampiamo dall'ultima alla prima.
 */
static void print_unsigned(uint32_t value, uint32_t base)
{
    /* Tabella per convertire un valore 0..15 nel carattere corrispondente.
     * digits[11] è 'b'. Una stringa letterale è un array di char: si può indicizzare. */
    const char *digits = "0123456789abcdef";

    /* Un numero a 32 bit ha al massimo 10 cifre in base 10 (4294967295).
     * `char buffer[10]` è un array di 10 char allocato SULLO STACK: non serve `new`
     * e sparisce da solo quando la funzione ritorna. */
    char buffer[10];
    int count = 0;

    /* do-while: così anche il valore 0 produce una cifra ("0"). */
    do {
        buffer[count] = digits[value % base];
        count++;
        value = value / base;
    } while (value != 0);

    /* Stampa le cifre in ordine inverso. */
    while (count > 0) {
        count--;
        kputchar(buffer[count]);
    }
}

/* Stampa un intero con segno in base 10. */
static void print_signed(int32_t value)
{
    if (value < 0) {
        kputchar('-');
        /*
         * Serve il valore assoluto, ma ATTENZIONE: scrivere `-value` è sbagliato per
         * il numero più piccolo, -2147483648, perché +2147483648 non sta in un int a
         * 32 bit. In Java il risultato "fa il giro" in modo definito; in C l'overflow
         * di un intero CON segno è "undefined behavior": il compilatore può fare
         * qualsiasi cosa. Gli interi SENZA segno invece fanno sempre il giro in modo
         * definito, quindi prima convertiamo a uint32_t e poi cambiamo segno.
         */
        print_unsigned(0 - (uint32_t) value, 10);
    } else {
        print_unsigned((uint32_t) value, 10);
    }
}

void kprintf(const char *format, ...)
{
    /*
     * Argomenti variabili. In Java `Object... args` è un vero array, con lunghezza e
     * tipi. In C no: gli argomenti extra sono semplicemente appoggiati sullo stack uno
     * dopo l'altro, e NESSUNO sa quanti sono né di che tipo. Lo deduciamo dalla stringa
     * di formato: per questo un `%s` a cui passi un numero manda in crash il kernel.
     *
     *   va_list args          = un "cursore" sugli argomenti extra
     *   va_start(args, format) = posiziona il cursore dopo l'ultimo parametro fisso
     *   va_arg(args, tipo)     = legge il prossimo argomento come `tipo` e avanza
     *   va_end(args)           = chiusura (obbligatoria per lo standard)
     */
    va_list args;
    va_start(args, format);

    /* Scorriamo la stringa di formato un carattere alla volta. */
    for (const char *p = format; *p != '\0'; p++) {

        /* Carattere normale: si stampa così com'è. */
        if (*p != '%') {
            kputchar(*p);
            continue;
        }

        /* Abbiamo trovato '%': il carattere successivo dice cosa stampare. */
        p++;

        switch (*p) {
        case 's':
            print_string(va_arg(args, const char *));
            break;

        case 'd':
            print_signed(va_arg(args, int32_t));
            break;

        case 'x':
            print_unsigned(va_arg(args, uint32_t), 16);
            break;

        case 'c':
            /* Negli argomenti variabili un `char` viene sempre "promosso" a `int`:
             * quindi si legge un int e poi lo si riconverte a char. */
            kputchar((char) va_arg(args, int));
            break;

        case '%':
            kputchar('%');
            break;

        case '\0':
            /* La stringa finisce subito dopo un '%' (es. "100%"): stampiamo il '%'
             * e usciamo, altrimenti il p++ del for salterebbe oltre la fine della
             * stringa e leggeremmo memoria a caso. */
            kputchar('%');
            va_end(args);
            return;

        default:
            /* Formato sconosciuto: lo stampiamo tale e quale, così l'errore si vede. */
            kputchar('%');
            kputchar(*p);
            break;
        }
    }

    va_end(args);
}
