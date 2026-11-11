/*
 * lib/kprintf.h — La "printf" del kernel.
 * Qui ci sono solo le firme; il codice è in lib/kprintf.c.
 */
#ifndef LIB_KPRINTF_H
#define LIB_KPRINTF_H

/* Stampa un carattere sia sullo schermo VGA sia sulla seriale. */
void kputchar(char c);

/*
 * Stampa una stringa formattata, come System.out.printf in Java.
 * Formati supportati:
 *   %s  stringa            %d  intero con segno in base 10
 *   %c  carattere          %x  intero senza segno in esadecimale (base 16)
 *   %%  il carattere '%'
 *
 * I tre puntini `...` indicano un numero variabile di argomenti
 * (come `Object... args` in Java, ma SENZA controlli: vedi kprintf.c).
 */
void kprintf(const char *format, ...);

#endif /* LIB_KPRINTF_H */
