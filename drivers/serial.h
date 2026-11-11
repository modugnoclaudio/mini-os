/*
 * drivers/serial.h — Driver della porta seriale COM1.
 * Qui ci sono solo le firme; il codice è in drivers/serial.c.
 */
#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

/* Configura COM1 (38400 baud, 8 bit di dati, nessuna parità, 1 bit di stop). */
void serial_init(void);

/* Invia un carattere su COM1. Converte '\n' in "\r\n" per i terminali. */
void serial_putchar(char c);

#endif /* DRIVERS_SERIAL_H */
