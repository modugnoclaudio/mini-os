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
#include "mm/pmm.h"
#include "mm/paging.h"
#include "kernel/interrupts.h"
#include "lib/kprintf.h"
#include "lib/string.h"

/* Definizione delle due variabili dichiarate `extern` nell'header. */
struct process *current_process = 0;
struct process *idle_process = 0;

static uint32_t next_pid = 0;       /* il prossimo PID da assegnare */

/* Definita in proc/usermode.asm: salta in modalità utente. Non ritorna mai. */
extern void enter_user_mode(uint32_t entry_point, uint32_t user_stack);

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
    idle->is_user = 0;
    idle->entry = 0;
    idle->directory = paging_get_kernel_directory();
    idle->quantum_left = SCHEDULER_QUANTUM;
    idle->cpu_ticks = 0;
    idle->wake_tick = 0;
    idle->exit_code = 0;
    idle->killed_by = NOT_KILLED;
    idle->next = idle;                  /* lista circolare con un solo elemento: punta a sé */

    idle_process = idle;
    current_process = idle;
}

/*
 * Il punto da cui parte ogni nuovo processo. Non viene mai chiamata con una
 * normale chiamata: ci si arriva con il `ret` di context_switch, grazie allo stack
 * costruito a mano in allocate_process (vedi sotto).
 * Quando si arriva qui lo scheduler ha già reso attivi lo spazio di indirizzi e lo
 * stack del kernel di questo processo.
 */
static void process_trampoline(void)
{
    if (current_process->is_user) {
        /*
         * Processo utente: si scende in modalità utente, all'inizio del programma.
         * Da qui in poi il kernel riprenderà il controllo solo attraverso un
         * interrupt, un'eccezione o una syscall.
         */
        enter_user_mode(USER_ENTRY_POINT, USER_STACK_TOP);
    }

    /* Thread del kernel. Si arriva qui dall'interno dello scheduler, che lavora a
     * interrupt spenti: un processo appena nato deve riaccenderli, altrimenti il
     * timer non potrebbe mai più togliergli la CPU. (Per un processo utente ci
     * pensa enter_user_mode.) */
    interrupts_enable();

    current_process->entry();

    /* Se la funzione del processo ritorna, il processo è finito. Non può esserci un
     * `return` da qui: sullo stack non c'è nessun indirizzo a cui tornare. */
    process_exit(0);
}

/*
 * La parte comune a thread del kernel e processi utente: alloca il PCB e lo stack
 * del kernel, e prepara lo stack perché il primo context switch porti in
 * process_trampoline. Il processo NON viene ancora messo nella lista.
 */
static struct process *allocate_process(const char *name)
{
    struct process *process = (struct process *) kmalloc(sizeof(struct process));
    if (process == 0) {
        return 0;
    }

    /* Ogni processo ha uno stack del kernel tutto suo: è ciò che gli permette di
     * avere le proprie variabili locali e la propria catena di chiamate. */
    process->stack = kmalloc(KERNEL_STACK_SIZE);
    if (process->stack == 0) {
        kfree(process);
        return 0;
    }

    string_copy(process->name, name, PROCESS_NAME_LENGTH);
    process->state = PROCESS_READY;
    process->is_user = 0;
    process->entry = 0;
    process->directory = paging_get_kernel_directory();
    process->quantum_left = SCHEDULER_QUANTUM;
    process->cpu_ticks = 0;
    process->wake_tick = 0;
    process->exit_code = 0;
    process->killed_by = NOT_KILLED;

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

    return process;
}

/*
 * Assegna il PID e inserisce il processo nella lista circolare, in fondo (cioè
 * subito prima del processo 0). Da questo momento lo scheduler può sceglierlo.
 * SEZIONE CRITICA: se il timer ci interrompesse a metà, lo scheduler seguirebbe
 * una lista agganciata solo per metà.
 */
static void add_to_list(struct process *process)
{
    uint32_t flags = interrupts_save_and_disable();

    process->pid = next_pid++;

    struct process *last = idle_process;
    while (last->next != idle_process) {    /* cerca chi punta al processo 0 */
        last = last->next;
    }
    last->next = process;
    process->next = idle_process;

    interrupts_restore(flags);
}

struct process *process_create(const char *name, void (*entry)(void))
{
    struct process *process = allocate_process(name);
    if (process == 0) {
        return 0;
    }

    process->entry = entry;

    add_to_list(process);
    return process;
}

/*
 * Mappa `page_count` pagine nuove e azzerate nello spazio di indirizzi indicato, a
 * partire dall'indirizzo virtuale `virtual_start`. Se `image` non è nullo, vi copia
 * dentro i primi `image_size` byte. Restituisce 1 se riesce, 0 se manca memoria.
 */
static int map_user_region(uint32_t *directory, uint32_t virtual_start, uint32_t page_count,
                           const uint8_t *image, uint32_t image_size)
{
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t frame = pmm_alloc_page();
        if (frame == 0) {
            return 0;
        }

        /*
         * La pagina va azzerata: potrebbe contenere dati vecchi di un altro
         * processo o del kernel, che il programma non deve poter leggere.
         *
         * Nota: scriviamo nella pagina usando `frame`, il suo indirizzo FISICO, che
         * grazie alla mappatura in identità è valido anche come indirizzo virtuale
         * del kernel. L'indirizzo utente (0x40000000...) qui non funzionerebbe: è
         * valido solo nello spazio del nuovo processo, che non è quello attivo ora.
         */
        memset((void *) frame, 0, PAGE_SIZE);

        /* Copia della fetta di programma che cade in questa pagina (se ce n'è). */
        uint32_t offset = i * PAGE_SIZE;
        if (image != 0 && offset < image_size) {
            uint32_t bytes = image_size - offset;
            if (bytes > PAGE_SIZE) {
                bytes = PAGE_SIZE;
            }
            memcpy((void *) frame, image + offset, bytes);
        }

        if (!paging_map_user_page(directory, virtual_start + offset, frame, PAGE_WRITABLE)) {
            pmm_free_page(frame);
            return 0;
        }
    }

    return 1;
}

struct process *process_create_user(const char *name, const uint8_t *image, uint32_t image_size)
{
    /* Il programma deve stare nello spazio che gli riserviamo. */
    if (image == 0 || image_size > USER_IMAGE_PAGES * PAGE_SIZE) {
        return 0;
    }

    /*
     * È davvero un programma? Controlliamo il numero magico nei primi 4 byte.
     * Senza questo controllo si potrebbe "eseguire" un file di testo: la CPU
     * interpreterebbe le lettere come istruzioni, con risultati imprevedibili.
     */
    const char *magic = PROGRAM_MAGIC;
    if (image_size < PROGRAM_MAGIC_LENGTH) {
        return 0;
    }
    for (uint32_t i = 0; i < PROGRAM_MAGIC_LENGTH; i++) {
        if (image[i] != (uint8_t) magic[i]) {
            return 0;
        }
    }

    struct process *process = allocate_process(name);
    if (process == 0) {
        return 0;
    }

    process->is_user = 1;

    /*
     * È il lavoro di un "loader": costruire lo spazio di indirizzi del processo e
     * copiarvi il programma.
     *   1. una page directory nuova, con il kernel già mappato e la zona utente vuota;
     *   2. le pagine per codice e dati, con dentro l'immagine del programma;
     *   3. le pagine per lo stack utente.
     */
    uint32_t *directory = paging_create_directory();

    int ok = (directory != 0)
          && map_user_region(directory, USER_CODE_BASE, USER_IMAGE_PAGES, image, image_size)
          && map_user_region(directory, USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE,
                             USER_STACK_PAGES, 0, 0);

    if (!ok) {
        /* Memoria esaurita a metà: si restituisce tutto ciò che era stato preso.
         * In C la pulizia in caso di errore va scritta a mano, ramo per ramo. */
        paging_destroy_directory(directory);
        kfree(process->stack);
        kfree(process);
        return 0;
    }

    process->directory = directory;

    add_to_list(process);
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
     * stack (e il proprio spazio di indirizzi) mentre esegue questa funzione. Si
     * segna quindi come terminato e cede la CPU per sempre; a fare pulizia sarà il
     * processo 0 (process_reap_terminated).
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

void process_kill_current(struct registers *regs)
{
    /*
     * Un programma utente ha fatto qualcosa di vietato. A differenza di un errore
     * del kernel (che porta al panic), qui il sistema è perfettamente sano: basta
     * eliminare il colpevole. È ciò che su Linux si vede come "Segmentation fault".
     *
     * COLLEGAMENTO CON LA TEORIA: è la protezione in azione. L'errore di un
     * processo non danneggia né il kernel né gli altri processi.
     */
    kprintf("  >>> processo %d (%s) terminato dal kernel: eccezione %d (%s) all'istruzione 0x%x\n",
            current_process->pid, current_process->name,
            regs->int_no, interrupt_exception_name(regs->int_no), regs->eip);

    current_process->killed_by = (int32_t) regs->int_no;
    process_exit(-1);
}

struct process *process_find(uint32_t pid)
{
    struct process *process = idle_process;
    do {
        if (process->pid == pid) {
            return process;
        }
        process = process->next;
    } while (process != idle_process);

    return 0;
}

void process_kill(struct process *process)
{
    uint32_t flags = interrupts_save_and_disable();

    /*
     * Con una sola CPU, se stiamo eseguendo questa funzione il processo bersaglio
     * NON sta girando: è fermo dentro una chiamata a schedule(). Basta quindi
     * segnarlo come terminato perché lo scheduler non lo scelga mai più; ciò che
     * resta sul suo stack viene semplicemente abbandonato.
     * È sicuro perché il nostro kernel non ha lock: un processo fermo non può
     * avere "in mano" una risorsa che resterebbe bloccata per sempre.
     */
    if (process != idle_process && process != current_process
        && process->state != PROCESS_TERMINATED) {
        process->exit_code = -1;
        process->state = PROCESS_TERMINATED;
    }

    interrupts_restore(flags);
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

            /* Un processo utente possiede anche uno spazio di indirizzi: la sua
             * directory, le sue page table e tutte le pagine del programma. */
            if (process->is_user) {
                paging_destroy_directory(process->directory);
            }

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
    case PROCESS_BLOCKED:    return "bloccato";
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
        kprintf("    pid %d  %s  (%s, %s, %d tick di CPU)\n",
                process->pid, process->name,
                process->is_user ? "utente" : "kernel",
                state_name(process->state), process->cpu_ticks);
        process = process->next;
    } while (process != idle_process);

    interrupts_restore(flags);
}
