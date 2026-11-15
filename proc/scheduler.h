/*
 * proc/scheduler.h — Lo scheduler: decide quale processo usa la CPU.
 * Il codice è in proc/scheduler.c.
 */
#ifndef PROC_SCHEDULER_H
#define PROC_SCHEDULER_H

#include <stdint.h>

/*
 * "Dichiarazione anticipata" (forward declaration): dice al compilatore solo che
 * ESISTE un tipo chiamato `struct process`, senza dire com'è fatto. Basta per
 * dichiarare funzioni che ricevono un PUNTATORE a quel tipo (un puntatore ha sempre
 * la stessa dimensione, qualunque cosa indichi).
 * Si usa al posto di `#include "proc/process.h"` per non creare un'inclusione
 * circolare: process.c include già questo header.
 * In Java il problema non esiste: le classi si vedono a vicenda senza dichiarazioni.
 */
struct process;

/* Il QUANTO DI TEMPO: per quanti tick del timer un processo può tenere la CPU prima
 * che gli venga tolta. Con il timer a 100 Hz, 5 tick = 50 millisecondi. */
#define SCHEDULER_QUANTUM 5

/*
 * Sceglie il prossimo processo e gli passa la CPU.
 * VA CHIAMATA A INTERRUPT DISABILITATI. Di solito non si chiama direttamente:
 * si usano le funzioni qui sotto.
 */
void schedule(void);

/* Chiamata dal gestore del timer a ogni tick: è qui che avviene la prelazione. */
void scheduler_tick(void);

/* Il processo corrente cede volontariamente la CPU, restando pronto. */
void process_yield(void);

/* Il processo corrente si addormenta per `ticks` tick del timer. */
void process_sleep(uint32_t ticks);

/*
 * BLOCCO IN ATTESA DI UN EVENTO. Il processo corrente esce dal giro dei pronti e
 * non riceve più la CPU finché qualcun altro non chiama process_wake() su di lui.
 * Chi si blocca deve ricontrollare la condizione al risveglio, in un ciclo:
 *
 *     while (cio' che aspetto non e' ancora successo) {
 *         process_block();
 *     }
 */
void process_block(void);

/* Risveglia un processo bloccato (lo rimette tra i pronti). Se il puntatore è nullo
 * o il processo non è bloccato non fa nulla. Si può chiamare da un gestore di interrupt. */
void process_wake(struct process *process);

/* Quanti cambi di contesto sono avvenuti dall'avvio. */
uint32_t scheduler_get_switch_count(void);

#endif /* PROC_SCHEDULER_H */
