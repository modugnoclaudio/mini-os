/*
 * drivers/timer.h — Driver del timer di sistema (PIT 8253/8254).
 * Il codice è in drivers/timer.c.
 */
#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include <stdint.h>

/* Quanti interrupt al secondo genera il timer: 100 Hz = un "tick" ogni 10 ms. */
#define TIMER_HZ 100

/* Programma il timer a TIMER_HZ interrupt al secondo e attiva l'IRQ 0. */
void timer_init(void);

/* Numero di tick trascorsi dall'avvio. */
uint32_t timer_get_ticks(void);

#endif /* DRIVERS_TIMER_H */
