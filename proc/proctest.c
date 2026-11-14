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
#include "mm/kheap.h"
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

    /*
     * Creiamo i due processi a interrupt spenti. Senza questa sezione critica, un
     * tick del timer arrivato subito dopo la prima process_create() farebbe partire
     * "ping" prima ancora che "pong" esista, e l'ordine dei turni non sarebbe più
     * prevedibile. (È successo davvero: vedi "Errori incontrati" nelle note della fase 4.)
     */
    uint32_t flags = interrupts_save_and_disable();

    struct process *ping = process_create("ping", ping_thread);
    struct process *pong = process_create("pong", pong_thread);
    check("creazione di due processi", ping != 0 && pong != 0);
    check("i processi ricevono PID diversi dal processo 0",
          ping != 0 && pong != 0 && ping->pid != 0 && pong->pid != ping->pid);

    /* Creare un processo non lo fa partire: è solo "pronto". Girerà quando lo
     * scheduler lo sceglierà, cioè non prima che riaccendiamo gli interrupt. */
    check("un processo appena creato non ha ancora girato", turn_log_length == 0);

    interrupts_restore(flags);

    while (finished_count < 2) {
        process_yield();
    }

    kprintf("  ordine dei turni: %s\n", turn_log);
    check("i due processi si sono alternati un turno a testa",
          string_equals(turn_log, "ababab"));

    process_reap_terminated();
    check("i processi terminati sono stati rimossi", process_count() == 1);
}

/* ---------------------------------------------------------------------------
 * Prova 2: multitasking CON PRELAZIONE, e processi che dormono
 * ---------------------------------------------------------------------------
 * Due processi "calcolano" senza mai cedere la CPU. Con il solo multitasking
 * cooperativo il primo la terrebbe fino alla fine; qui invece il timer la toglie
 * a ciascuno ogni SCHEDULER_QUANTUM tick, e i due avanzano insieme.
 * Un terzo processo passa quasi tutto il tempo a dormire.
 */

#define WORK_STEPS      3       /* passi di lavoro di ogni processo                 */
#define TICKS_PER_STEP  10      /* tick di CPU consumati a ogni passo (100 ms)      */

/* Per verificare la prelazione: quanti passi aveva già fatto B quando A ha finito. */
static volatile uint32_t steps_done_by_b = 0;
static volatile uint32_t b_steps_when_a_finished = 0;

/*
 * Consuma CPU finché questo processo non ha accumulato altri `ticks` tick di CPU.
 * È un'attesa ATTIVA voluta: simula un calcolo lungo ("CPU-bound"). Il ciclo non
 * contiene nessuna chiamata che ceda la CPU: se gli altri processi riescono a
 * girare, è solo merito della prelazione.
 * Il conteggio è sui tick di CPU del processo stesso (cpu_ticks), non sul tempo
 * reale: mentre il processo è fermo in attesa del suo turno, il conto non avanza.
 */
static void burn_cpu(uint32_t ticks)
{
    /* `volatile`: cpu_ticks viene aggiornato dal gestore del timer, quindi va
     * riletto dalla memoria a ogni giro del ciclo. */
    volatile uint32_t *cpu_ticks = &current_process->cpu_ticks;
    uint32_t target = *cpu_ticks + ticks;

    while (*cpu_ticks < target) {
        /* niente: stiamo "calcolando" */
    }
}

static void cpu_a_thread(void)
{
    for (int i = 1; i <= WORK_STEPS; i++) {
        burn_cpu(TICKS_PER_STEP);
        kprintf("  [calcolo-A] passo %d di %d\n", i, WORK_STEPS);
        log_turn('A');
    }
    b_steps_when_a_finished = steps_done_by_b;
    mark_finished();
}

static void cpu_b_thread(void)
{
    for (int i = 1; i <= WORK_STEPS; i++) {
        burn_cpu(TICKS_PER_STEP);
        kprintf("  [calcolo-B] passo %d di %d\n", i, WORK_STEPS);
        log_turn('B');
        steps_done_by_b++;      /* la modifica solo questo processo: niente sezione critica */
    }
    mark_finished();
}

static void sleeper_thread(void)
{
    for (int i = 1; i <= WORK_STEPS; i++) {
        process_sleep(15);      /* 150 ms senza consumare CPU */
        kprintf("  [dormiglione] sveglia %d di %d\n", i, WORK_STEPS);
        log_turn('s');

        /* Alla prima sveglia mostriamo l'elenco: i due calcoli sono nel pieno del lavoro. */
        if (i == 1) {
            process_print_list();
        }
    }
    mark_finished();
}

void proctest_preemptive(void)
{
    kprintf("Prova della prelazione (quanto di %d tick) e di sleep:\n", SCHEDULER_QUANTUM);
    reset_shared_data();
    steps_done_by_b = 0;
    b_steps_when_a_finished = 0;

    uint32_t switches_before = scheduler_get_switch_count();
    uint32_t heap_used_before = kheap_get_used_bytes();

    struct process *a = process_create("calcolo-A", cpu_a_thread);
    struct process *b = process_create("calcolo-B", cpu_b_thread);
    struct process *sleeper = process_create("dormiglione", sleeper_thread);
    check("creazione di tre processi", a != 0 && b != 0 && sleeper != 0);

    /*
     * Il processo 0 aspetta, da bravo processo idle: `hlt` ferma la CPU fino al
     * prossimo interrupt. Non cede mai la CPU esplicitamente: è il timer che,
     * a ogni tick, vede che ci sono processi pronti e gliela toglie.
     */
    while (finished_count < 3) {
        __asm__ volatile ("hlt");
    }

    /* Il PCB di un processo terminato resta valido finché non chiamiamo
     * process_reap_terminated(): leggiamo ora ciò che ci serve. Farlo DOPO sarebbe
     * un "use after free". */
    uint32_t sleeper_cpu_ticks = sleeper->cpu_ticks;

    uint32_t switches = scheduler_get_switch_count() - switches_before;

    kprintf("  ordine dei turni: %s\n", turn_log);
    kprintf("  cambi di contesto durante la prova: %d\n", switches);

    check("tutti e tre i processi hanno finito", finished_count == 3);
    check("B ha fatto progressi prima che A finisse (prelazione)",
          b_steps_when_a_finished > 0);
    check("ci sono stati molti cambi di contesto", switches >= 10);
    check("il dormiglione ha consumato pochissima CPU", sleeper_cpu_ticks <= 3);

    process_reap_terminated();
    check("i processi terminati sono stati rimossi", process_count() == 1);

    /* PCB e stack dei tre processi erano memoria presa con kmalloc: dopo la pulizia
     * l'heap deve essere tornato esattamente com'era. */
    check("nessun memory leak: l'heap e' tornato come prima",
          kheap_get_used_bytes() == heap_used_before);
}

void proctest_summary(void)
{
    if (failures == 0) {
        kprintf("Processi: tutte le prove superate\n");
    } else {
        kprintf("Processi: %d prove FALLITE\n", failures);
    }
}
