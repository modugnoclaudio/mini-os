/*
 * proc/programs.c — Avvio di un programma a partire da un file.
 *
 * Fa da ponte tra il filesystem e i processi: dato un NOME, trova il file e ne fa
 * un processo utente. È la parte "trova l'eseguibile" di ciò che sui sistemi Unix
 * fa la chiamata exec.
 *
 * (Nella fase 5 qui c'era una tabella fissa di programmi incorporati nel kernel;
 * ora i programmi sono normali file del filesystem in RAM.)
 */

#include <stdint.h>
#include "proc/programs.h"
#include "proc/process.h"
#include "fs/ramfs.h"
#include "kernel/interrupts.h"

struct process *program_run(const char *name)
{
    /*
     * SEZIONE CRITICA: tra il momento in cui troviamo il file e quello in cui
     * process_create_user ha finito di copiarne il contenuto nelle pagine del nuovo
     * processo, nessuno deve poter cancellare o sostituire il file. Altrimenti
     * `file->data` punterebbe a memoria già liberata (un "use after free").
     */
    uint32_t flags = interrupts_save_and_disable();

    struct process *process = 0;

    const struct file *file = fs_find(name);
    if (file != 0) {
        /* process_create_user controlla anche che il file sia davvero un programma. */
        process = process_create_user(name, file->data, file->size);
    }

    interrupts_restore(flags);
    return process;
}
