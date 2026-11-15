/*
 * drivers/keyboard.h — Driver della tastiera PS/2.
 * Il codice è in drivers/keyboard.c.
 */
#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

/* Registra il gestore dell'IRQ 1 e attiva la linea. Da quel momento i tasti premuti
 * vengono accumulati in un buffer, in attesa che un processo li legga. */
void keyboard_init(void);

/*
 * Restituisce il prossimo carattere digitato. Se non ce ne sono, BLOCCA il
 * processo che la chiama finché non viene premuto un tasto.
 * Non va chiamata dal processo 0 (che non può bloccarsi).
 */
char keyboard_read_char(void);

/* Restituisce 1 se nel buffer non c'è nessun carattere in attesa. */
int keyboard_buffer_is_empty(void);

/*
 * Inserisce un carattere nel buffer come se fosse stato digitato.
 * Serve alle prove automatiche (kernel/autotest.c), che così possono "scrivere"
 * dei comandi senza una tastiera vera.
 */
void keyboard_inject_char(char c);

/* Come sopra, per la combinazione Ctrl+C. */
void keyboard_inject_ctrl_c(void);

#endif /* DRIVERS_KEYBOARD_H */
