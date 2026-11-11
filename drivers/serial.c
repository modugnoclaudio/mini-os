/*
 * drivers/serial.c — Scrittura sulla porta seriale COM1.
 *
 * PERCHÉ LA SERIALE: è il dispositivo più semplice che esista per far uscire del
 * testo dalla macchina. Con `-serial stdio` QEMU collega COM1 al terminale da cui
 * lo lanciamo: i log del kernel si leggono (e si copiano) comodamente lì, e restano
 * visibili anche se lo schermo VGA si rompe o scorre via.
 *
 * COME FUNZIONA: il chip della seriale (UART 16550) si comanda con 8 porte di I/O
 * consecutive a partire da 0x3F8. Ogni porta è un "registro" del chip.
 */

#include <stdint.h>
#include "drivers/serial.h"
#include "drivers/io.h"

#define COM1 0x3F8      /* porta base di COM1 su tutti i PC */

/* Registri dell'UART, espressi come distanza (offset) dalla porta base. */
#define UART_DATA        0   /* dato da inviare / ricevuto                       */
#define UART_INT_ENABLE  1   /* quali interrupt genera il chip                   */
#define UART_FIFO_CTRL   2   /* controllo delle code interne (FIFO)              */
#define UART_LINE_CTRL   3   /* formato dei dati (bit, parità, stop) + bit DLAB  */
#define UART_MODEM_CTRL  4   /* linee di controllo del modem                     */
#define UART_LINE_STATUS 5   /* stato: il bit 5 dice "posso accettare un byte"   */

void serial_init(void)
{
    /* Niente interrupt dalla seriale: per ora la usiamo "a polling" (vedi sotto). */
    outb(COM1 + UART_INT_ENABLE, 0x00);

    /*
     * Velocità. L'UART divide una frequenza base di 115200 baud per un "divisore".
     * Divisore 3 -> 115200 / 3 = 38400 baud.
     * Per scrivere il divisore si accende il bit 7 (DLAB) del registro LINE_CTRL:
     * finché è acceso, le porte +0 e +1 non sono più "dato" e "interrupt" ma
     * il byte basso e il byte alto del divisore.
     */
    outb(COM1 + UART_LINE_CTRL, 0x80);  /* DLAB acceso                        */
    outb(COM1 + 0, 0x03);               /* divisore, byte basso = 3           */
    outb(COM1 + 1, 0x00);               /* divisore, byte alto  = 0           */

    /* Formato "8N1": 8 bit di dati, Nessuna parità, 1 bit di stop.
     * Scrivendo 0x03 si spegne anche il DLAB (bit 7 = 0). */
    outb(COM1 + UART_LINE_CTRL, 0x03);

    /* Abilita e svuota le code FIFO interne del chip. */
    outb(COM1 + UART_FIFO_CTRL, 0xC7);

    /* Attiva le linee DTR e RTS ("sono pronto a trasmettere"). */
    outb(COM1 + UART_MODEM_CTRL, 0x03);
}

/* Restituisce un valore diverso da 0 se l'UART può accettare un nuovo byte.
 * Nota: il C "classico" non ha boolean: 0 è falso, qualsiasi altro numero è vero. */
static int serial_can_transmit(void)
{
    /* `& 0x20` isola il bit 5 del registro di stato (0x20 = 0010 0000 in binario). */
    return inb(COM1 + UART_LINE_STATUS) & 0x20;
}

/*
 * Invia un byte così com'è, aspettando che il chip sia pronto.
 *
 * ATTESA ATTIVA (busy waiting / polling): giriamo a vuoto finché il chip non è
 * pronto. È la tecnica che a lezione si confronta con gli interrupt: spreca CPU,
 * ma è semplicissima e funziona anche prima di aver configurato gli interrupt.
 */
static void serial_write_byte(char c)
{
    while (!serial_can_transmit()) {
        /* niente: aspettiamo */
    }

    outb(COM1 + UART_DATA, (uint8_t) c);
}

void serial_putchar(char c)
{
    /* I terminali vogliono "\r\n" (ritorno carrello + a capo) per andare a inizio riga.
     * La chiamata ricorsiva invia '\r' e poi si prosegue con '\n'. */
    if (c == '\n') {
        serial_putchar('\r');
    }

    /* Backspace: un terminale con '\b' sposta solo il cursore a sinistra. Per cancellare
     * davvero il carattere si manda "indietro, spazio, indietro". Qui inviamo i primi
     * due; il terzo '\b' è quello inviato in fondo alla funzione. */
    if (c == '\b') {
        serial_write_byte('\b');
        serial_write_byte(' ');
    }

    serial_write_byte(c);
}
