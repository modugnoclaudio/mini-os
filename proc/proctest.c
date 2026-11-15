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
#include "mm/pmm.h"
#include "proc/programs.h"
#include "drivers/keyboard.h"
#include "kernel/syscall_numbers.h"
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

/* ---------------------------------------------------------------------------
 * Prova 3: il meccanismo delle syscall
 * ---------------------------------------------------------------------------
 * Le syscall sono pensate per i programmi utente, ma `int 0x80` si può eseguire
 * anche dal kernel: è un modo semplice di provare il percorso
 * IDT -> stub -> tabella delle syscall -> valore di ritorno in EAX
 * prima di avere dei veri programmi utente.
 */

/* Esegue una syscall con un argomento e ne restituisce il risultato.
 * "a"(number) mette il numero in EAX, "b"(argument) l'argomento in EBX;
 * "=a"(result) legge il risultato da EAX dopo l'interrupt. */
static int32_t do_syscall(uint32_t number, uint32_t argument)
{
    int32_t result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "a"(number), "b"(argument)
                      : "memory");
    return result;
}

void proctest_syscalls(void)
{
    kprintf("Prova delle syscall (int 0x80):\n");

    check("getpid dal processo 0 restituisce 0",
          do_syscall(SYS_GETPID, 0) == (int32_t) current_process->pid);
    check("una syscall con numero inesistente restituisce -1",
          do_syscall(9999, 0) == -1);
    check("yield ritorna normalmente", do_syscall(SYS_YIELD, 0) == 0);
}

/* ---------------------------------------------------------------------------
 * Prova 4: programmi in MODALITÀ UTENTE
 * ---------------------------------------------------------------------------
 * I programmi sono i file .c nella cartella user/: compilati a parte, incorporati nel kernel come
 * dati e caricati ciascuno nel proprio spazio di indirizzi.
 */

/* Aspetta che un processo termini. Lo si legge attraverso un puntatore `volatile`
 * perché il campo `state` viene cambiato da un altro flusso di esecuzione.
 * Il PCB resta valido anche dopo la fine del processo, finché il processo 0
 * non chiama process_reap_terminated(). */
static void wait_for_exit(struct process *process)
{
    volatile enum process_state *state = &process->state;

    while (*state != PROCESS_TERMINATED) {
        if (current_process == idle_process) {
            /* Il processo 0 non può dormire: aspetta il prossimo interrupt. */
            __asm__ volatile ("hlt");
        } else {
            /* Un processo normale si mette in attesa e ricontrolla al tick dopo. */
            process_sleep(1);
        }
    }
}

/*
 * Il "supervisore" della prova con il programma che non cede mai la CPU.
 *
 * Perché un thread a parte e non il processo 0, come per le altre prove? Perché il
 * processo 0 è l'idle: lo scheduler lo sceglie SOLO quando nessun altro è pronto.
 * Finché 'ciclo' gira (cioè sempre) il processo 0 non riceve mai la CPU, e non
 * potrebbe né accorgersi che 'hello' ha finito né eliminare 'ciclo'.
 * Un thread normale invece partecipa al giro del round-robin come tutti.
 */
static void supervisor_thread(void)
{
    struct process *spinner = program_run("ciclo");
    struct process *polite = program_run("hello");
    check("'ciclo' (che non termina mai) e un secondo 'hello' avviati",
          spinner != 0 && polite != 0);

    if (spinner != 0 && polite != 0) {
        /* 'hello' può girare e finire solo se il timer toglie la CPU a 'ciclo'. */
        wait_for_exit(polite);
        check("'hello' ha finito anche se 'ciclo' non cede mai la CPU (prelazione)",
              polite->exit_code == 0);

        /* Dormiamo 20 tick: in quel tempo l'unico processo pronto è 'ciclo'. */
        uint32_t ticks_before = spinner->cpu_ticks;
        process_sleep(20);
        check("mentre gli altri dormono, 'ciclo' consuma tutta la CPU",
              spinner->cpu_ticks >= ticks_before + 15);

        /* Non finirà mai da solo: lo eliminiamo d'autorità. */
        process_kill(spinner);
        check("'ciclo' e' stato terminato dal kernel",
              spinner->state == PROCESS_TERMINATED);
    }
    /* Ritornando, questo thread termina. */
}

void proctest_user_programs(void)
{
    kprintf("Prova dei programmi in modalita' utente:\n");

    uint32_t free_pages_before = pmm_get_free_pages();
    uint32_t heap_used_before = kheap_get_used_bytes();

    /* --- Un programma semplice: parte, stampa con una syscall, termina --- */
    struct process *hello = program_run("hello");
    check("il programma 'hello' viene caricato", hello != 0 && hello->is_user);
    if (hello != 0) {
        wait_for_exit(hello);
        check("'hello' e' terminato da solo con codice 0",
              hello->exit_code == 0 && hello->killed_by == NOT_KILLED);
    }

    process_reap_terminated();
    check("terminato il programma, tutta la sua memoria e' tornata libera",
          pmm_get_free_pages() == free_pages_before &&
          kheap_get_used_bytes() == heap_used_before);

    check("un programma inesistente non parte", program_run("non-esiste") == 0);

    /* --- Due copie dello stesso programma: memoria separata --- */
    struct process *first = program_run("conta");
    struct process *second = program_run("conta");
    check("due copie di 'conta' in esecuzione insieme", first != 0 && second != 0);
    if (first != 0 && second != 0) {
        wait_for_exit(first);
        wait_for_exit(second);

        /* Ogni copia somma 3 volte il proprio PID in una variabile globale che,
         * nelle due copie, sta allo stesso indirizzo virtuale. */
        check("ogni copia ha contato per conto suo (memoria separata)",
              first->exit_code == (int32_t) (3 * first->pid) &&
              second->exit_code == (int32_t) (3 * second->pid));
    }
    process_reap_terminated();

    /* --- Programmi che violano le regole: devono morire loro, non il kernel --- */
    struct process *intruder = program_run("invasore");
    if (intruder != 0) {
        wait_for_exit(intruder);
        check("chi scrive nella memoria del kernel viene terminato (page fault)",
              intruder->killed_by == INT_PAGE_FAULT);
    }

    struct process *privileged = program_run("privilegiato");
    if (privileged != 0) {
        wait_for_exit(privileged);
        check("chi esegue un'istruzione privilegiata viene terminato (eccezione 13)",
              privileged->killed_by == 13);
    }

    /* --- Un programma che non cede mai la CPU: prelazione in modalità utente --- */
    struct process *supervisor = process_create("supervisore", supervisor_thread);
    check("avvio del thread supervisore", supervisor != 0);
    if (supervisor != 0) {
        wait_for_exit(supervisor);
    }

    process_reap_terminated();
    check("dopo tutto questo resta solo il processo 0, e il kernel e' vivo",
          process_count() == 1);
    check("nessuna pagina di memoria e' andata persa",
          pmm_get_free_pages() == free_pages_before);
}

/* ---------------------------------------------------------------------------
 * Prova 5: lettura BLOCCANTE dalla tastiera
 * ---------------------------------------------------------------------------
 * Un processo chiede un carattere quando non ce ne sono: deve bloccarsi (senza
 * consumare CPU) e ripartire solo quando il carattere arriva. I tasti vengono
 * "premuti" con keyboard_inject_char, che usa la stessa strada del vero interrupt.
 */

static char received_text[8];
static volatile uint32_t received_count = 0;

static void reader_thread(void)
{
    /* Legge tre caratteri: ogni chiamata blocca il processo finché non ne arriva uno. */
    for (int i = 0; i < 3; i++) {
        received_text[i] = keyboard_read_char();
        received_text[i + 1] = '\0';
        received_count++;
    }
    mark_finished();
}

/* Lascia passare qualche tick, dando agli altri processi il tempo di girare. */
static void wait_ticks(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        __asm__ volatile ("hlt");
    }
}

void proctest_keyboard(void)
{
    kprintf("Prova della lettura bloccante dalla tastiera:\n");
    reset_shared_data();
    received_count = 0;
    received_text[0] = '\0';

    struct process *reader = process_create("lettore", reader_thread);
    check("creazione del processo lettore", reader != 0);
    if (reader == 0) {
        return;
    }

    wait_ticks(5);
    check("senza tasti premuti il lettore e' bloccato",
          reader->state == PROCESS_BLOCKED && received_count == 0);
    check("un processo bloccato non consuma CPU", reader->cpu_ticks <= 1);

    keyboard_inject_char('o');
    wait_ticks(5);
    check("arrivato un carattere, il lettore lo riceve e si blocca di nuovo",
          received_count == 1 && reader->state == PROCESS_BLOCKED);

    /* Due caratteri di fila: restano nel buffer finché il lettore non li prende. */
    keyboard_inject_char('k');
    keyboard_inject_char('!');
    wait_for_exit(reader);

    kprintf("  il lettore ha ricevuto: %s\n", received_text);
    check("i caratteri arrivano tutti e nell'ordine giusto (coda FIFO)",
          string_equals(received_text, "ok!"));
    check("alla fine il buffer e' vuoto", keyboard_buffer_is_empty());

    process_reap_terminated();
}

void proctest_summary(void)
{
    if (failures == 0) {
        kprintf("Processi: tutte le prove superate\n");
    } else {
        kprintf("Processi: %d prove FALLITE\n", failures);
    }
}
