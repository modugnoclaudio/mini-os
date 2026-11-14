/*
 * proc/process.c — Creazione, terminazione ed elenco dei processi.
 *
 * Tutti i PCB sono collegati in una LISTA CIRCOLARE: l'ultimo punta di nuovo al
 * primo. È la struttura naturale per lo scheduler round-robin ("a giro"), che deve
 * solo seguire i puntatori `next` senza mai trovare una fine.
 *
 *        +--------+     +--------+     +--------+
 *   +--> | pid 0  | --> | pid 1  | --> | pid 2  | --+
 *   |    | (idle) |     |        |     |        |   |
 *   |    +--------+     +--------+     +--------+   |
 *   +-----------------------------------------------+
 *
 * Chi sceglie quale processo far girare è lo scheduler, in proc/scheduler.c.
 */

#include <stdint.h>
#include "proc/process.h"
#include "proc/scheduler.h"
#include "mm/kheap.h"
#include "kernel/interrupts.h"
#include "lib/kprintf.h"
#include "lib/string.h"

/* Definizione delle due variabili dichiarate `extern` nell'header. */
struct process *current_process = 0;
struct process *idle_process = 0;

static uint32_t next_pid = 0;       /* il prossimo PID da assegnare */

void process_init(void)
{
    /*
     * Il processo 0 non viene "creato": esiste già. È il flusso di esecuzione
     * partito da boot.asm, che sta girando in questo momento sullo stack definito lì.
     * Gli diamo solo un PCB, così lo scheduler lo tratta come tutti gli altri.
     *
     * Diventerà il processo "idle" (inattivo): quello che gira, eseguendo `hlt`,
     * quando nessun altro ha qualcosa da fare. Deve esistere sempre: la CPU non
     * può "non eseguire niente".
     */
    struct process *idle = (struct process *) kmalloc(sizeof(struct process));

    idle->pid = next_pid++;
    string_copy(idle->name, "idle", PROCESS_NAME_LENGTH);
    idle->state = PROCESS_RUNNING;      /* sta girando adesso */
    idle->esp = 0;                      /* verrà scritto al primo context switch */
    idle->stack = 0;                    /* usa lo stack di boot.asm: non è nostro da liberare */
    idle->entry = 0;
    idle->quantum_left = SCHEDULER_QUANTUM;
    idle->cpu_ticks = 0;
    idle->wake_tick = 0;
    idle->exit_code = 0;
    idle->next = idle;                  /* lista circolare con un solo elemento: punta a sé */

    idle_process = idle;
    current_process = idle;
}

/*
 * Il punto da cui parte ogni nuovo processo. Non viene mai chiamata con una
 * normale chiamata: ci si arriva con il `ret` di context_switch, grazie allo stack
 * costruito a mano in process_create (vedi sotto).
 */
static void process_trampoline(void)
{
    /* Si arriva qui dall'interno dello scheduler, che lavora a interrupt spenti.
     * Un processo che è già stato in esecuzione li riaccende da solo quando riprende;
     * uno appena nato deve farlo esplicitamente, altrimenti il timer non potrebbe
     * mai più togliergli la CPU. */
    interrupts_enable();

    current_process->entry();

    /* Se la funzione del processo ritorna, il processo è finito. Non può esserci un
     * `return` da qui: sullo stack non c'è nessun indirizzo a cui tornare. */
    process_exit(0);
}

struct process *process_create(const char *name, void (*entry)(void))
{
    struct process *process = (struct process *) kmalloc(sizeof(struct process));
    if (process == 0) {
        return 0;
    }

    /* Ogni processo ha uno stack tutto suo: è ciò che gli permette di avere le
     * proprie variabili locali e la propria catena di chiamate. */
    process->stack = kmalloc(KERNEL_STACK_SIZE);
    if (process->stack == 0) {
        kfree(process);
        return 0;
    }

    string_copy(process->name, name, PROCESS_NAME_LENGTH);
    process->entry = entry;
    process->state = PROCESS_READY;
    process->quantum_left = SCHEDULER_QUANTUM;
    process->cpu_ticks = 0;
    process->wake_tick = 0;
    process->exit_code = 0;

    /*
     * IL TRUCCO DELLO STACK INIZIALE.
     * context_switch riprende un processo facendo 4 pop e un `ret`. Un processo
     * nuovo non ha mai chiamato context_switch, quindi sul suo stack non c'è nulla
     * da riprendere. Lo costruiamo noi, identico a come lo lascerebbe una chiamata vera:
     *
     *     indirizzi alti    +----------------------+ <- fine dello stack
     *                       | process_trampoline   |  <- il `ret` salterà qui
     *                       | 0   (per EBP)        |
     *                       | 0   (per EBX)        |
     *                       | 0   (per ESI)        |
     *     indirizzi bassi   | 0   (per EDI)        |  <- process->esp
     *                       +----------------------+
     *
     * Lo stack cresce verso il basso: si parte dalla FINE della memoria allocata
     * e si scende. `*(--sp) = x` è esattamente ciò che fa `push x`: prima sposta
     * il puntatore di un elemento verso il basso, poi ci scrive.
     */
    uint32_t *sp = (uint32_t *) ((uint8_t *) process->stack + KERNEL_STACK_SIZE);

    *(--sp) = (uint32_t) process_trampoline;    /* indirizzo di "ritorno" */
    *(--sp) = 0;                                /* EBP */
    *(--sp) = 0;                                /* EBX */
    *(--sp) = 0;                                /* ESI */
    *(--sp) = 0;                                /* EDI */

    process->esp = (uint32_t) sp;

    /*
     * Inserimento nella lista circolare, in fondo (cioè subito prima del processo 0).
     * SEZIONE CRITICA: se il timer ci interrompesse a metà, lo scheduler seguirebbe
     * una lista agganciata solo per metà.
     */
    uint32_t flags = interrupts_save_and_disable();

    process->pid = next_pid++;

    struct process *last = idle_process;
    while (last->next != idle_process) {    /* cerca chi punta al processo 0 */
        last = last->next;
    }
    last->next = process;
    process->next = idle_process;

    interrupts_restore(flags);

    return process;
}

void process_exit(int32_t exit_code)
{
    /* Il processo 0 non può terminare: serve sempre qualcuno che possa girare. */
    if (current_process == idle_process) {
        kprintf("ERRORE: il processo 0 ha chiamato process_exit\n");
        return;
    }

    interrupts_disable();

    /*
     * Un processo non può liberare la propria memoria: sta ancora USANDO il proprio
     * stack mentre esegue questa funzione. Si segna quindi come terminato e cede la
     * CPU per sempre; a fare pulizia sarà il processo 0 (process_reap_terminated).
     * Nei sistemi Unix un processo in questo stato si chiama "zombie".
     */
    current_process->exit_code = exit_code;
    current_process->state = PROCESS_TERMINATED;
    schedule();

    /* Qui non si arriva mai: lo scheduler non sceglie un processo terminato. */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void process_reap_terminated(void)
{
    uint32_t flags = interrupts_save_and_disable();

    /* Classica rimozione da una lista concatenata: si tiene anche il puntatore al
     * nodo precedente, per poter "scavalcare" quello da eliminare. */
    struct process *previous = idle_process;
    struct process *process = idle_process->next;

    while (process != idle_process) {
        /* Non si libera mai il processo corrente: sta usando il proprio stack. */
        if (process->state == PROCESS_TERMINATED && process != current_process) {
            previous->next = process->next;     /* sgancia dalla lista */

            kfree(process->stack);
            kfree(process);

            process = previous->next;
        } else {
            previous = process;
            process = process->next;
        }
    }

    interrupts_restore(flags);
}

uint32_t process_count(void)
{
    uint32_t flags = interrupts_save_and_disable();

    uint32_t count = 0;
    struct process *process = idle_process;
    do {                                /* do-while: la lista circolare ha sempre almeno un elemento */
        count++;
        process = process->next;
    } while (process != idle_process);

    interrupts_restore(flags);
    return count;
}

/* Traduce lo stato in una parola. `switch` su un enum, come in Java. */
static const char *state_name(enum process_state state)
{
    switch (state) {
    case PROCESS_READY:      return "pronto";
    case PROCESS_RUNNING:    return "in esecuzione";
    case PROCESS_SLEEPING:   return "in attesa";
    case PROCESS_TERMINATED: return "terminato";
    }
    return "?";
}

void process_print_list(void)
{
    /* Sezione critica: l'elenco deve essere una fotografia coerente, non cambiare
     * mentre lo stiamo stampando. */
    uint32_t flags = interrupts_save_and_disable();

    kprintf("  Elenco dei processi:\n");

    struct process *process = idle_process;
    do {
        kprintf("    pid %d  %s  (%s, %d tick di CPU)\n",
                process->pid, process->name,
                state_name(process->state), process->cpu_ticks);
        process = process->next;
    } while (process != idle_process);

    interrupts_restore(flags);
}
