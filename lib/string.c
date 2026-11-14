/*
 * lib/string.c — memset e memcpy scritte a mano.
 *
 * Versioni volutamente semplici: un byte alla volta. Quelle vere copiano 4 o più
 * byte per volta e sono molto più veloci, ma anche molto meno leggibili.
 */

#include <stdint.h>
#include "lib/string.h"

void *memset(void *destination, int value, uint32_t count)
{
    /* Non si può scrivere attraverso un `void *`: il compilatore non sa quanto è
     * grande "la cosa" puntata. Lo convertiamo in un puntatore a byte (uint8_t). */
    uint8_t *bytes = (uint8_t *) destination;

    for (uint32_t i = 0; i < count; i++) {
        bytes[i] = (uint8_t) value;
    }

    return destination;
}

void *memcpy(void *destination, const void *source, uint32_t count)
{
    uint8_t *to = (uint8_t *) destination;
    const uint8_t *from = (const uint8_t *) source;

    for (uint32_t i = 0; i < count; i++) {
        to[i] = from[i];
    }

    return destination;
}

void string_copy(char *destination, const char *source, uint32_t size)
{
    if (size == 0) {
        return;
    }

    uint32_t i = 0;
    while (i < size - 1 && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';     /* una stringa C deve sempre finire con lo zero */
}

int string_equals(const char *a, const char *b)
{
    /* Avanza finché i caratteri coincidono e nessuna delle due stringhe è finita. */
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }

    /* Sono uguali solo se ci siamo fermati alla fine di ENTRAMBE. */
    return *a == *b;
}
