/*
 * proc/programs.h — L'elenco dei programmi utente incorporati nel kernel.
 * Il codice è in proc/programs.c; i byte dei programmi in proc/user_programs.asm.
 */
#ifndef PROC_PROGRAMS_H
#define PROC_PROGRAMS_H

#include <stdint.h>
#include "proc/process.h"

/* Avvia come processo utente il programma con quel nome (es. "hello").
 * Restituisce il PCB del nuovo processo, o 0 se il programma non esiste o manca memoria. */
struct process *program_run(const char *name);

#endif /* PROC_PROGRAMS_H */
