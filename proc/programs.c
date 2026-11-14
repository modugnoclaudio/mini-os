/*
 * proc/programs.c — Tabella dei programmi utente disponibili.
 *
 * Fa da ponte tra un NOME ("hello") e i BYTE del programma, che stanno dentro il
 * kernel (proc/user_programs.asm). È un sostituto provvisorio del filesystem:
 * nella fase 6 i programmi si cercheranno tra i file.
 */

#include <stdint.h>
#include "proc/programs.h"
#include "proc/process.h"
#include "lib/string.h"

/*
 * Le etichette definite in proc/user_programs.asm. Non sono variabili con un
 * contenuto: sono solo INDIRIZZI (inizio e fine di ogni programma). Dichiararle
 * come array di byte permette di usarne il nome come puntatore al primo byte.
 */
extern const uint8_t user_program_hello_start[];
extern const uint8_t user_program_hello_end[];
extern const uint8_t user_program_conta_start[];
extern const uint8_t user_program_conta_end[];
extern const uint8_t user_program_invasore_start[];
extern const uint8_t user_program_invasore_end[];
extern const uint8_t user_program_privilegiato_start[];
extern const uint8_t user_program_privilegiato_end[];
extern const uint8_t user_program_ciclo_start[];
extern const uint8_t user_program_ciclo_end[];

struct program {
    const char *name;
    const uint8_t *start;       /* primo byte del programma                 */
    const uint8_t *end;         /* primo byte DOPO la fine del programma    */
};

static const struct program programs[] = {
    { "hello",        user_program_hello_start,        user_program_hello_end },
    { "conta",        user_program_conta_start,        user_program_conta_end },
    { "invasore",     user_program_invasore_start,     user_program_invasore_end },
    { "privilegiato", user_program_privilegiato_start, user_program_privilegiato_end },
    { "ciclo",        user_program_ciclo_start,        user_program_ciclo_end },
};

/* Numero di elementi dell'array: dimensione totale diviso dimensione di un elemento.
 * È l'idioma del C per ottenere ciò che in Java è `programs.length`
 * (funziona solo sugli array veri, non sui puntatori). */
#define PROGRAM_COUNT (sizeof(programs) / sizeof(programs[0]))

struct process *program_run(const char *name)
{
    for (uint32_t i = 0; i < PROGRAM_COUNT; i++) {
        if (string_equals(programs[i].name, name)) {
            /* La differenza tra due puntatori allo stesso array dà il numero di
             * elementi tra loro: qui, la dimensione del programma in byte. */
            uint32_t size = (uint32_t) (programs[i].end - programs[i].start);

            return process_create_user(name, programs[i].start, size);
        }
    }

    return 0;   /* nessun programma con quel nome */
}
