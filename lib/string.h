/*
 * lib/string.h — Funzioni di base per lavorare su blocchi di memoria.
 * Il codice è in lib/string.c.
 *
 * Hanno lo stesso nome e lo stesso comportamento di quelle della libreria standard
 * del C (che noi non abbiamo). Non è solo comodità: il compilatore gcc, anche con
 * -ffreestanding, a volte inserisce di sua iniziativa chiamate a `memset` e `memcpy`
 * (per esempio per azzerare o copiare una struct). Se non esistessero, il linker
 * darebbe l'errore "undefined reference to memset".
 */
#ifndef LIB_STRING_H
#define LIB_STRING_H

#include <stdint.h>

/*
 * `void *` = "puntatore a qualcosa, non importa a cosa": un indirizzo senza tipo.
 * È il modo del C per scrivere funzioni che lavorano su qualsiasi dato
 * (un po' come un parametro di tipo `Object` in Java, ma senza alcun controllo).
 */

/* Riempie `count` byte a partire da `destination` con il valore `value`.
 * Restituisce `destination` (come la versione standard). */
void *memset(void *destination, int value, uint32_t count);

/* Copia `count` byte da `source` a `destination`. Le due zone NON devono sovrapporsi. */
void *memcpy(void *destination, const void *source, uint32_t count);

/*
 * Copia la stringa `source` in `destination`, che ha spazio per `size` byte in tutto.
 * Copia al massimo size - 1 caratteri e aggiunge SEMPRE il terminatore '\0':
 * se la stringa è troppo lunga viene troncata, ma non si scrive mai fuori dal buffer.
 * (La `strcpy` della libreria standard non fa questo controllo: è una delle cause
 * più famose di falle di sicurezza, il "buffer overflow".)
 */
void string_copy(char *destination, const char *source, uint32_t size);

/* Restituisce 1 se le due stringhe hanno gli stessi caratteri, 0 altrimenti.
 * In C `a == b` tra due stringhe confronta gli INDIRIZZI, non il contenuto
 * (come `==` tra due String in Java): serve una funzione, come `equals`. */
int string_equals(const char *a, const char *b);

#endif /* LIB_STRING_H */
