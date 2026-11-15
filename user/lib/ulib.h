/*
 * user/lib/ulib.h — La piccola libreria dei programmi utente.
 * Il codice è in user/lib/ulib.c.
 *
 * È la nostra "libreria standard" in miniatura: l'unico modo che un programma ha
 * per farsi fare qualcosa dal kernel. Un programma utente NON può includere gli
 * header del kernel né chiamarne le funzioni: gira in un altro spazio di indirizzi
 * e con la CPU in modalità non privilegiata.
 *
 * Unica eccezione apparente: lib/string.h. Quelle funzioni (memcpy, string_equals...)
 * non parlano con l'hardware né con il kernel: sono puro calcolo. Lo stesso codice
 * viene quindi compilato DUE volte, una copia dentro il kernel e una dentro ogni
 * programma. Non è codice condiviso a tempo di esecuzione.
 */
#ifndef USER_LIB_ULIB_H
#define USER_LIB_ULIB_H

#include <stdint.h>
#include "lib/string.h"

/* Lunghezza massima del nome di un file, terminatore compreso (come FS_NAME_LENGTH
 * nel kernel). */
#define FILE_NAME_LENGTH 32

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

/* Attende che venga premuto un tasto e ne restituisce il carattere. */
char read_key(void);

/* Avvia il programma contenuto nel file `name`. Restituisce il PID del nuovo
 * processo, oppure -1 se il file non esiste o non è un programma. */
int spawn(const char *name);

/* Attende la fine del processo `pid` (avviato con spawn) e ne restituisce il
 * codice di uscita. */
int wait(int pid);

/* Fa stampare al kernel l'elenco dei processi. */
void ps(void);

/* Copia in `name` (grande `name_size` byte) il nome del file numero `index` e ne
 * restituisce la dimensione in byte; -1 se non ci sono così tanti file. */
int file_info(uint32_t index, char *name, uint32_t name_size);

/* Legge al massimo `size` byte del file `name`, a partire dalla posizione `offset`,
 * mettendoli in `buffer`. Restituisce i byte letti (0 = fine del file), -1 se il
 * file non esiste. */
int file_read(const char *name, void *buffer, uint32_t size, uint32_t offset);

/* Crea il file `name` con quel contenuto, o lo sostituisce se esiste già.
 * Restituisce 0 se riesce, -1 altrimenti. */
int file_write(const char *name, const void *data, uint32_t size);

/* Cancella il file `name`. Restituisce 0 se riesce, -1 se non esiste. */
int file_delete(const char *name);

/* ---- Funzioni di comodo, costruite sopra le syscall ---- */

void print(const char *text);           /* stampa una stringa terminata da '\0' */
void print_number(int value);           /* stampa un intero in base 10          */
void print_hex(uint32_t value);         /* stampa un numero in esadecimale, con 0x davanti */

/*
 * Legge una riga dalla tastiera, mostrando i caratteri man mano e gestendo il
 * tasto Backspace. Termina con Invio. La riga (senza l'a-capo) finisce in `buffer`,
 * che ha spazio per `size` byte. Restituisce la lunghezza della riga.
 */
uint32_t read_line(char *buffer, uint32_t size);

#endif /* USER_LIB_ULIB_H */
