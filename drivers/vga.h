/*
 * drivers/vga.h — Driver dello schermo in modalità testo VGA (80 colonne x 25 righe).
 * Qui ci sono solo le firme; il codice è in drivers/vga.c.
 */
#ifndef DRIVERS_VGA_H
#define DRIVERS_VGA_H

/* Pulisce lo schermo e porta il cursore in alto a sinistra. Va chiamata per prima. */
void vga_init(void);

/* Stampa un carattere alla posizione del cursore. Gestisce '\n', '\b' (backspace) e lo scorrimento. */
void vga_putchar(char c);

/*
 * Nota: in C `f(void)` significa "nessun parametro". Scrivere solo `f()` (come in Java)
 * in C vuol dire "parametri non specificati", ed è meglio evitarlo.
 */

#endif /* DRIVERS_VGA_H */
