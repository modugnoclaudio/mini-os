/*
 * drivers/vga.c — Scrittura a schermo in modalità testo VGA.
 *
 * COME FUNZIONA: in modalità testo lo schermo è una griglia di 80x25 caselle.
 * La scheda video "specchia" una zona di memoria che parte dall'indirizzo fisico
 * 0xB8000: ogni casella è un numero a 16 bit
 *
 *      bit 15..8 = colore (4 bit sfondo + 4 bit testo)     bit 7..0 = codice ASCII
 *
 * Scrivere in quella memoria fa comparire i caratteri: non serve nessuna "chiamata".
 * Questa tecnica si chiama memory-mapped I/O (I/O mappato in memoria).
 */

#include <stdint.h>
#include "drivers/vga.h"
#include "drivers/io.h"

/* #define crea una costante sostituita dal preprocessore prima della compilazione
 * (simile a `static final int` in Java, ma è una pura sostituzione di testo). */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_COLOR   0x07        /* sfondo nero (0), testo grigio chiaro (7) */

/*
 * Puntatore alla memoria video.
 *
 * In Java non puoi decidere DOVE sta un dato in memoria. In C sì: il cast
 * `(uint16_t *) 0xB8000` dice "tratta il numero 0xB8000 come l'indirizzo di un
 * array di numeri a 16 bit". Poi `vga_buffer[i]` legge/scrive la casella i-esima,
 * cioè l'indirizzo 0xB8000 + i*2 (ogni elemento occupa 2 byte: l'aritmetica dei
 * puntatori tiene conto da sola della dimensione dell'elemento).
 *
 * `volatile` = "questa memoria ha effetti fuori dal programma": il compilatore non
 * deve eliminare né riordinare le scritture, anche se nessuno le rilegge.
 *
 * `static` su una variabile globale = visibile solo in questo file
 * (l'equivalente più vicino a `private` in Java).
 */
static volatile uint16_t *vga_buffer = (volatile uint16_t *) 0xB8000;

/* Posizione corrente del cursore (dove finirà il prossimo carattere). */
static int cursor_row = 0;
static int cursor_col = 0;

/* Costruisce il valore a 16 bit di una casella: colore nel byte alto, carattere nel basso.
 * `<< 8` sposta i bit di 8 posizioni a sinistra, `|` (OR bit a bit) li unisce. */
static uint16_t vga_entry(char c)
{
    /* Il cast a uint8_t evita che un char "negativo" (>127) sporchi il byte del colore. */
    return (uint16_t) ((VGA_COLOR << 8) | (uint8_t) c);
}

/*
 * Sposta il cursore lampeggiante hardware nella posizione corrente.
 * Il cursore è gestito dalla scheda video e si comanda tramite due porte di I/O:
 * su 0x3D4 si scrive QUALE registro interno si vuole modificare, su 0x3D5 il valore.
 * I registri 14 e 15 contengono la posizione (byte alto e byte basso).
 */
static void vga_update_cursor(void)
{
    /* La griglia 2D è memorizzata come array 1D: indice = riga * larghezza + colonna. */
    uint16_t pos = (uint16_t) (cursor_row * VGA_WIDTH + cursor_col);

    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t) (pos >> 8));      /* byte alto */
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t) (pos & 0xFF));    /* byte basso (& 0xFF tiene solo gli 8 bit bassi) */
}

/* Fa scorrere lo schermo di una riga verso l'alto e svuota l'ultima riga. */
static void vga_scroll(void)
{
    /* Copia ogni casella dalla riga sotto: la riga 1 va nella 0, la 2 nella 1, ... */
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }

    /* Riempie l'ultima riga di spazi. */
    for (int col = 0; col < VGA_WIDTH; col++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = vga_entry(' ');
    }

    cursor_row = VGA_HEIGHT - 1;
}

void vga_init(void)
{
    /* Riempie tutto lo schermo di spazi (cancella i messaggi del BIOS). */
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ');
    }

    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
}

void vga_putchar(char c)
{
    if (c == '\n') {
        /* A capo: prima colonna della riga successiva. */
        cursor_col = 0;
        cursor_row++;
    } else {
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c);
        cursor_col++;

        /* Fine riga: si va a capo automaticamente. */
        if (cursor_col == VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    /* Fine schermo: si scorre.
     * ATTENZIONE: in C nessuno controlla gli indici degli array. Scrivere oltre la
     * casella 80*25 non lancerebbe una ArrayIndexOutOfBoundsException come in Java:
     * sovrascriverebbe in silenzio altra memoria. Il controllo dobbiamo farlo noi. */
    if (cursor_row == VGA_HEIGHT) {
        vga_scroll();
    }

    vga_update_cursor();
}
