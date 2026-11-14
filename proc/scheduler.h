/*
 * proc/scheduler.h — Lo scheduler: decide quale processo usa la CPU.
 * Il codice è in proc/scheduler.c.
 */
#ifndef PROC_SCHEDULER_H
#define PROC_SCHEDULER_H

#include <stdint.h>

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

/* Quanti cambi di contesto sono avvenuti dall'avvio. */
uint32_t scheduler_get_switch_count(void);

#endif /* PROC_SCHEDULER_H */
