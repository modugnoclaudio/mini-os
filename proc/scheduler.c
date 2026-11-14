/*
 * proc/scheduler.c — Scheduler ROUND-ROBIN con prelazione.
 *
 * COLLEGAMENTO CON LA TEORIA: è l'algoritmo "a giro" visto a lezione.
 *   - I processi pronti vengono serviti a turno, nell'ordine della lista circolare.
 *   - Ognuno riceve un QUANTO di tempo (SCHEDULER_QUANTUM tick).
 *   - PRELAZIONE: se allo scadere del quanto il processo sta ancora girando, il
 *     timer gli toglie la CPU d'autorità. Nessun processo può monopolizzarla.
 *   - Un processo può anche lasciare la CPU prima: cedendola (process_yield),
 *     addormentandosi (process_sleep) o terminando (process_exit).
 *
 * Il processo 0 ("idle") è speciale: viene scelto solo quando nessun altro è pronto.
 *
 * I TRE MODI IN CUI SI ARRIVA A schedule():
 *
 *   interrupt del timer -> scheduler_tick() --+
 *   process_yield() / process_sleep() --------+--> schedule() --> context_switch()
 *   process_exit() ---------------------------+
 */

#include <stdint.h>
#include "proc/scheduler.h"
#include "proc/process.h"
#include "drivers/timer.h"
#include "kernel/interrupts.h"
#include "kernel/gdt.h"
#include "mm/paging.h"

/* Definita in proc/switch.asm. */
extern void context_switch(uint32_t *old_esp, uint32_t new_esp);

static uint32_t switch_count = 0;

/* Rimette "pronti" i processi addormentati il cui tempo di attesa è finito. */
static void wake_sleeping_processes(void)
{
    uint32_t now = timer_get_ticks();

    struct process *process = idle_process;
    do {
        if (process->state == PROCESS_SLEEPING && now >= process->wake_tick) {
            process->state = PROCESS_READY;
        }
        process = process->next;
    } while (process != idle_process);
}

/*
 * La POLITICA di scheduling: chi gira adesso?
 * Round-robin: il primo processo pronto che si incontra seguendo la lista, partendo
 * da quello DOPO il corrente. Così chi ha appena usato la CPU finisce in fondo al giro.
 */
static struct process *pick_next(void)
{
    struct process *start = current_process->next;
    struct process *candidate = start;

    /* Un giro completo: parte da dopo il corrente e termina con il corrente stesso
     * (che, se è ancora pronto, viene quindi considerato per ultimo). */
    do {
        if (candidate != idle_process && candidate->state == PROCESS_READY) {
            return candidate;
        }
        candidate = candidate->next;
    } while (candidate != start);

    /* Nessuno è pronto: gira il processo 0, che metterà la CPU a riposo con `hlt`. */
    return idle_process;
}

void schedule(void)
{
    struct process *previous = current_process;

    /* Il processo uscente, se stava semplicemente girando, torna tra i pronti.
     * Se invece si è addormentato o è terminato, il suo stato è già stato cambiato
     * da chi ha chiamato schedule(), e va lasciato com'è. */
    if (previous->state == PROCESS_RUNNING) {
        previous->state = PROCESS_READY;
    }

    struct process *next = pick_next();

    next->state = PROCESS_RUNNING;
    next->quantum_left = SCHEDULER_QUANTUM;     /* quanto nuovo di zecca */

    /* Se tocca di nuovo allo stesso processo non c'è niente da commutare. */
    if (next == previous) {
        return;
    }

    current_process = next;
    switch_count++;

    /*
     * Due cose in più rispetto al semplice cambio di stack, necessarie da quando
     * esistono i processi utente.
     *
     * 1) Lo STACK DEL KERNEL da usare al prossimo ingresso nel kernel. Quando un
     *    interrupt o una syscall interrompe un programma utente, la CPU legge dal
     *    TSS dove mettere i registri: deve essere la cima dello stack del kernel
     *    del processo che sta per girare. (Il processo 0 non ha uno stack allocato
     *    da noi e non va mai in modalità utente: per lui non serve.)
     */
    if (next->stack != 0) {
        gdt_set_kernel_stack((uint32_t) next->stack + KERNEL_STACK_SIZE);
    }

    /*
     * 2) Lo SPAZIO DI INDIRIZZI. Se il nuovo processo ha una page directory diversa
     *    la carichiamo in CR3: da qui in poi gli indirizzi della zona utente portano
     *    alla SUA memoria. Tra due thread del kernel la directory è la stessa e
     *    il cambio si salta (e con lui lo svuotamento del TLB).
     *    Il kernel resta raggiungibile in ogni caso: è mappato uguale in tutte le
     *    directory, compreso lo stack su cui stiamo lavorando in questo momento.
     */
    if (next->directory != previous->directory) {
        paging_switch_directory(next->directory);
    }

    /*
     * Il MECCANISMO: salva i registri di `previous` e carica quelli di `next`.
     * Questa chiamata "ritorna" solo quando qualcuno, in futuro, sceglierà di nuovo
     * `previous`: per lui sarà come se la funzione avesse semplicemente impiegato
     * molto tempo. Nel frattempo girano altri processi.
     *
     * `&previous->esp` = l'INDIRIZZO del campo esp del PCB: context_switch ci
     * scriverà dentro il valore di ESP da salvare.
     */
    context_switch(&previous->esp, next->esp);
}

void scheduler_tick(void)
{
    /* Il timer parte prima che i processi siano inizializzati: in quel caso non
     * c'è ancora nulla da schedulare. */
    if (current_process == 0) {
        return;
    }

    wake_sleeping_processes();

    current_process->cpu_ticks++;       /* contabilità: questo tick lo ha consumato lui */

    /* Il processo 0 non ha diritto a un quanto: appena qualcuno è pronto gli si
     * toglie la CPU, quindi per lui si controlla a ogni tick. */
    if (current_process == idle_process) {
        schedule();
        return;
    }

    /* PRELAZIONE: quanto esaurito, si passa al prossimo. */
    current_process->quantum_left--;
    if (current_process->quantum_left == 0) {
        schedule();
    }
}

void process_yield(void)
{
    /* schedule() manipola la lista dei processi e cambia stack: non deve essere
     * interrotta dal timer, che chiamerebbe schedule() a sua volta. */
    uint32_t flags = interrupts_save_and_disable();
    schedule();
    /* Qui si riprende quando lo scheduler ci sceglie di nuovo. */
    interrupts_restore(flags);
}

void process_sleep(uint32_t ticks)
{
    /* Il processo 0 non può dormire: dev'essere sempre disponibile. */
    if (current_process == idle_process) {
        return;
    }

    uint32_t flags = interrupts_save_and_disable();

    /* È un'attesa PASSIVA: il processo esce dal giro dei pronti e non consuma CPU
     * finché il timer non lo risveglia. L'alternativa sbagliata sarebbe un ciclo
     * `while (non è ancora ora) {}`, cioè attesa attiva. */
    current_process->state = PROCESS_SLEEPING;
    current_process->wake_tick = timer_get_ticks() + ticks;
    schedule();

    interrupts_restore(flags);
}

uint32_t scheduler_get_switch_count(void)
{
    return switch_count;
}
