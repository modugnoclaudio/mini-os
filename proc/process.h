/*
 * proc/process.h — I processi del kernel e il loro descrittore (PCB).
 * Il codice è in proc/process.c.
 *
 * NOTA SUI TERMINI: per ora i nostri "processi" sono in realtà THREAD DEL KERNEL:
 * ognuno ha il proprio stack e i propri registri, ma condividono tutti la stessa
 * memoria (quella del kernel). Diventeranno processi veri, con memoria separata e
 * modalità utente, nella fase 5.
 */
#ifndef PROC_PROCESS_H
#define PROC_PROCESS_H

#include <stdint.h>

#define PROCESS_NAME_LENGTH 16
#define KERNEL_STACK_SIZE   8192    /* 8 KiB di stack per ogni processo */

/*
 * Gli stati di un processo: è il diagramma degli stati visto a lezione.
 *
 *                 scelto dallo scheduler
 *      PRONTO  ------------------------->  IN ESECUZIONE  ---> TERMINATO
 *        ^  ^  <-------------------------        |
 *        |  |     quanto scaduto / yield         | process_sleep()
 *        |  |                                    v
 *        |  +------------------------------  IN ATTESA
 *        |        il tempo è passato
 *        +--- process_create()
 *
 * `enum` = un tipo con un elenco chiuso di valori, come gli enum di Java.
 * In C però sono semplici numeri interi (0, 1, 2...) con un nome: nessun metodo.
 */
enum process_state {
    PROCESS_READY,          /* pronto: potrebbe girare, aspetta il suo turno        */
    PROCESS_RUNNING,        /* in esecuzione: sta usando la CPU adesso              */
    PROCESS_SLEEPING,       /* in attesa: dorme fino a un certo istante             */
    PROCESS_TERMINATED      /* terminato: aspetta solo che la sua memoria sia liberata */
};

/*
 * Il PCB (Process Control Block): tutto ciò che il kernel sa di un processo.
 * Ce n'è uno per processo, allocato con kmalloc.
 */
struct process {
    uint32_t pid;                       /* identificatore unico                         */
    char name[PROCESS_NAME_LENGTH];     /* nome, solo per i messaggi                    */
    enum process_state state;

    /*
     * Il CONTESTO salvato. Quando il processo non è in esecuzione, i suoi registri
     * stanno in cima al SUO stack; qui ricordiamo solo dove (il valore di ESP).
     * È tutto ciò che serve per riprenderlo: vedi proc/switch.asm.
     */
    uint32_t esp;

    void *stack;                        /* inizio della memoria dello stack (0 per il processo 0) */
    void (*entry)(void);                /* la funzione che il processo esegue           */

    uint32_t quantum_left;              /* tick rimasti prima di cedere la CPU          */
    uint32_t cpu_ticks;                 /* tick di CPU consumati in totale              */
    uint32_t wake_tick;                 /* se dorme: a quale tick va svegliato          */

    struct process *next;               /* prossimo PCB: la lista è CIRCOLARE           */
};

/*
 * Variabili globali definite in proc/process.c e usate anche dallo scheduler.
 * `extern` in un header significa: "questa variabile esiste, ma è definita in un
 * altro file .c; qui la dichiaro soltanto, così chi include l'header può usarla".
 * (In Java sarebbe un campo `static` pubblico di un'altra classe.)
 */
extern struct process *current_process;     /* il processo che sta usando la CPU        */
extern struct process *idle_process;        /* il processo 0, che gira quando nessun altro può */

/* Trasforma il flusso di esecuzione attuale (kernel_main) nel processo 0. */
void process_init(void);

/* Crea un nuovo processo che eseguirà `entry`. Restituisce il PCB, o 0 se manca memoria. */
struct process *process_create(const char *name, void (*entry)(void));

/* Termina il processo corrente. Non ritorna mai. */
void process_exit(void);

/* Libera la memoria dei processi terminati. La chiama il processo 0. */
void process_reap_terminated(void);

/* Quanti processi esistono (compreso il processo 0). */
uint32_t process_count(void);

/* Stampa l'elenco dei processi con stato e tempo di CPU (sarà il comando `ps`). */
void process_print_list(void);

#endif /* PROC_PROCESS_H */
