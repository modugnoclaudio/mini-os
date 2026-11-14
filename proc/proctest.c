/*
 * proc/proctest.c — Prove automatiche di processi e scheduler.
 *
 * Qui si vede per la prima volta più di un flusso di esecuzione. Ogni funzione
 * `..._thread` è il "main" di un processo: viene passata a process_create() e gira
 * sul proprio stack, alternandosi con le altre.
 *
 * PARAGONE CON JAVA: process_create("ping", ping_thread) somiglia a
 * `new Thread(this::pingThread).start()`. La differenza è che qui stiamo scrivendo
 * noi ciò che in Java fanno la JVM e il sistema operativo.
 */

#include <stdint.h>
#include "proc/proctest.h"
#include "proc/process.h"
#include "proc/scheduler.h"
#include "kernel/interrupts.h"
#include "lib/kprintf.h"
#include "lib/string.h"

static int failures = 0;

/* Controlla una condizione e ne stampa l'esito (l'equivalente di assertTrue). */
static void check(const char *description, int condition)
{
    if (condition) {
        kprintf("  ok: %s\n", description);
    } else {
        kprintf("  ERRORE: %s\n", description);
        failures++;
    }
}

/* ---------------------------------------------------------------------------
 * Dati condivisi tra i processi di prova
 * ---------------------------------------------------------------------------
 * Tutti i processi del kernel vedono le stesse variabili globali (condividono la
 * memoria). `volatile` perché vengono modificate da un processo e lette da un
 * altro: il compilatore deve rileggerle ogni volta.
 */

/* Quanti processi di prova hanno finito. */
static volatile uint32_t finished_count = 0;

/* Il "diario" dei turni: ogni processo vi aggiunge la propria lettera quando gira.
 * Alla fine la stringa racconta in che ordine si sono alternati. */
#define TURN_LOG_SIZE 64
static char turn_log[TURN_LOG_SIZE];
static volatile uint32_t turn_log_length = 0;

/*
 * Le due funzioni seguenti modificano dati condivisi, quindi usano una SEZIONE
 * CRITICA. Anche un banale `finished_count++` non è un'operazione unica per la CPU:
 * è "leggi, somma 1, riscrivi". Se il timer cambiasse processo tra la lettura e la
 * scrittura, e anche l'altro processo incrementasse, uno dei due incrementi
 * andrebbe perso. È l'esempio classico di race condition.
 */
static void mark_finished(void)
{
    uint32_t flags = interrupts_save_and_disable();
    finished_count++;
    interrupts_restore(flags);
}

static void log_turn(char who)
{
    uint32_t flags = interrupts_save_and_disable();
    if (turn_log_length < TURN_LOG_SIZE - 1) {
        turn_log[turn_log_length] = who;
        turn_log_length++;
        turn_log[turn_log_length] = '\0';   /* la stringa resta sempre terminata */
    }
    interrupts_restore(flags);
}

static void reset_shared_data(void)
{
    finished_count = 0;
    turn_log_length = 0;
    turn_log[0] = '\0';
}

/* ---------------------------------------------------------------------------
 * Prova 1: multitasking COOPERATIVO
 * ---------------------------------------------------------------------------
 * Due processi fanno un passo a testa e poi cedono la CPU di propria volontà.
 * Nessuno viene interrotto: collaborano. (Era il modello di Windows 3.1 e del
 * primo Mac OS; il difetto è che basta un processo che non cede mai per bloccare tutto.)
 */

static void ping_thread(void)
{
    /* `i` è una variabile locale: vive sullo stack di QUESTO processo. È per questo
     * che ogni processo ha bisogno di uno stack suo: ping e pong hanno ciascuno
     * la propria `i`, che sopravvive mentre gira l'altro. */
    for (int i = 1; i <= 3; i++) {
        kprintf("  [ping] turno %d\n", i);
        log_turn('a');
        process_yield();
    }
    mark_finished();
    /* Il `return` finale riporta in process_trampoline, che chiama process_exit(). */
}

static void pong_thread(void)
{
    for (int i = 1; i <= 3; i++) {
        kprintf("  [pong] turno %d\n", i);
        log_turn('b');
        process_yield();
    }
    mark_finished();
}

void proctest_cooperative(void)
{
    kprintf("Prova dei processi cooperativi (yield):\n");
    reset_shared_data();

    struct process *ping = process_create("ping", ping_thread);
    struct process *pong = process_create("pong", pong_thread);
    check("creazione di due processi", ping != 0 && pong != 0);
    check("i processi ricevono PID diversi dal processo 0",
          ping != 0 && pong != 0 && ping->pid != 0 && pong->pid != ping->pid);

    /* Finora non è successo nulla: i due processi sono "pronti" ma non hanno mai
     * girato. Partono solo quando noi (il processo 0) cediamo la CPU. */
    check("prima dello yield nessuno ha ancora girato", turn_log_length == 0);

    while (finished_count < 2) {
        process_yield();
    }

    kprintf("  ordine dei turni: %s\n", turn_log);
    check("i due processi si sono alternati un turno a testa",
          string_equals(turn_log, "ababab"));

    process_reap_terminated();
    check("i processi terminati sono stati rimossi", process_count() == 1);
}

void proctest_summary(void)
{
    if (failures == 0) {
        kprintf("Processi: tutte le prove superate\n");
    } else {
        kprintf("Processi: %d prove FALLITE\n", failures);
    }
}
