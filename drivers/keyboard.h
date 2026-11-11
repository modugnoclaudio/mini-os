/*
 * drivers/keyboard.h — Driver della tastiera PS/2.
 * Il codice è in drivers/keyboard.c.
 */
#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

/* Registra il gestore dell'IRQ 1 e attiva la linea. Da quel momento ogni tasto
 * premuto viene stampato a schermo (e sulla seriale). */
void keyboard_init(void);

#endif /* DRIVERS_KEYBOARD_H */
