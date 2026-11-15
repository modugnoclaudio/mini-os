/*
 * proc/programs.h — Avvio di un programma a partire da un file.
 * Il codice è in proc/programs.c.
 */
#ifndef PROC_PROGRAMS_H
#define PROC_PROGRAMS_H

#include <stdint.h>
#include "proc/process.h"

/*
 * Avvia come processo utente il programma contenuto nel file con quel nome
 * (es. "hello"). Restituisce il PCB del nuovo processo, oppure 0 se il file non
 * esiste, non è un programma o manca memoria.
 */
struct process *program_run(const char *name);

#endif /* PROC_PROGRAMS_H */
