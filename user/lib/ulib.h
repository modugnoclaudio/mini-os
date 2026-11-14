/*
 * user/lib/ulib.h — La piccola libreria dei programmi utente.
 * Il codice è in user/lib/ulib.c.
 *
 * È la nostra "libreria standard" in miniatura: l'unico modo che un programma ha
 * per farsi fare qualcosa dal kernel. Un programma utente NON può includere gli
 * header del kernel né chiamarne le funzioni: gira in un altro spazio di indirizzi
 * e con la CPU in modalità non privilegiata.
 */
#ifndef USER_LIB_ULIB_H
#define USER_LIB_ULIB_H

#include <stdint.h>

/* ---- Chiamate di sistema: ognuna fa una `int 0x80` (vedi ulib.c) ---- */

/* Termina il processo con il codice indicato. Non ritorna. */
void exit(int exit_code);

/* Stampa `length` caratteri a partire da `text`. Restituisce i caratteri stampati,
 * oppure -1 se il kernel rifiuta la richiesta. */
int write(const char *text, uint32_t length);

/* Restituisce il PID di questo processo. */
int getpid(void);

/* Sospende il processo per `ticks` tick del timer (100 tick = 1 secondo). */
void sleep(uint32_t ticks);

/* Cede la CPU agli altri processi. */
void yield(void);

/* ---- Funzioni di comodo, costruite sopra write() ---- */

void print(const char *text);           /* stampa una stringa terminata da '\0' */
void print_number(int value);           /* stampa un intero in base 10          */
void print_hex(uint32_t value);         /* stampa un numero in esadecimale, con 0x davanti */

#endif /* USER_LIB_ULIB_H */
