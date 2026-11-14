/*
 * proc/process.h — I processi e il loro descrittore (PCB).
 * Il codice è in proc/process.c.
 *
 * Esistono due tipi di processo:
 *   - THREAD DEL KERNEL: eseguono una funzione del kernel, in modalità kernel,
 *     e condividono tutti la memoria del kernel (fase 4);
 *   - PROCESSI UTENTE: eseguono un programma caricato a parte, in modalità utente,
 *     ciascuno nel proprio spazio di indirizzi (fase 5).
 * Lo scheduler li tratta allo stesso modo.
 */
#ifndef PROC_PROCESS_H
#define PROC_PROCESS_H

#include <stdint.h>
#include "kernel/interrupts.h"

#define PROCESS_NAME_LENGTH 16
#define KERNEL_STACK_SIZE   8192    /* 8 KiB di stack del kernel per ogni processo */

/*
 * La disposizione della memoria di un processo utente (indirizzi VIRTUALI, uguali
 * per tutti i processi: ognuno ha il suo spazio di indirizzi).
 *
 *   0x40000000  +---------------------------+  USER_CODE_BASE
 *               | codice e dati del         |
 *               | programma (64 KiB)        |
 *   0x40010000  +---------------------------+
 *               |        (non mappato)      |
 *   0xAFFFC000  +---------------------------+
 *               | stack utente (16 KiB)     |  cresce verso il basso
 *   0xB0000000  +---------------------------+  USER_STACK_TOP
 *
 * USER_CODE_BASE deve coincidere con l'indirizzo scritto in user/user.ld.
 */
#define USER_CODE_BASE    0x40000000
#define USER_IMAGE_PAGES  16            /* 16 pagine = 64 KiB per codice e dati */
#define USER_STACK_TOP    0xB0000000
#define USER_STACK_PAGES  4             /* 4 pagine = 16 KiB di stack utente    */

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

/* Valore di `killed_by` per un processo che è terminato di propria volontà. */
#define NOT_KILLED (-1)

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
     * stanno in cima al SUO stack del kernel; qui ricordiamo solo dove (il valore
     * di ESP). È tutto ciò che serve per riprenderlo: vedi proc/switch.asm.
     */
    uint32_t esp;

    /*
     * Lo STACK DEL KERNEL del processo (0 per il processo 0, che usa quello di boot.asm).
     * Un processo utente ha DUE stack: quello utente, nella sua memoria, per il
     * proprio codice; e questo, usato quando si entra nel kernel per una syscall o
     * un interrupt. Il kernel non può fidarsi dello stack di un programma.
     */
    void *stack;

    int is_user;                        /* 1 = processo utente, 0 = thread del kernel   */
    void (*entry)(void);                /* thread del kernel: la funzione da eseguire   */

    /* Lo SPAZIO DI INDIRIZZI: la page directory da caricare in CR3 quando il
     * processo gira. I thread del kernel usano tutti quella del kernel. */
    uint32_t *directory;

    uint32_t quantum_left;              /* tick rimasti prima di cedere la CPU          */
    uint32_t cpu_ticks;                 /* tick di CPU consumati in totale              */
    uint32_t wake_tick;                 /* se dorme: a quale tick va svegliato          */

    int32_t exit_code;                  /* valore con cui il processo è terminato       */
    int32_t killed_by;                  /* numero dell'eccezione che lo ha fatto terminare,
                                           oppure NOT_KILLED                            */

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

/* Crea un thread del kernel che eseguirà `entry`. Restituisce il PCB, o 0 se manca memoria. */
struct process *process_create(const char *name, void (*entry)(void));

/*
 * Crea un processo utente a partire dall'immagine di un programma: `image` punta
 * ai byte del programma (un binario piatto, vedi user/user.ld) e `image_size` è la
 * loro quantità. Restituisce il PCB, o 0 se il programma è troppo grande o manca memoria.
 */
struct process *process_create_user(const char *name, const uint8_t *image, uint32_t image_size);

/* Termina il processo corrente con il codice indicato (per convenzione 0 = tutto
 * bene, come il valore restituito dal main in C o System.exit in Java). Non ritorna mai. */
void process_exit(int32_t exit_code);

/* Termina d'autorità il processo corrente perché ha provocato un'eccezione
 * (accesso a memoria non sua, istruzione vietata...). Non ritorna mai. */
void process_kill_current(struct registers *regs);

/* Termina un ALTRO processo (non quello corrente, non il processo 0), che in quel
 * momento è per forza fermo. La sua memoria verrà liberata da process_reap_terminated. */
void process_kill(struct process *process);

/* Libera la memoria dei processi terminati. La chiama il processo 0. */
void process_reap_terminated(void);

/* Quanti processi esistono (compreso il processo 0). */
uint32_t process_count(void);

/* Stampa l'elenco dei processi con stato e tempo di CPU (sarà il comando `ps`). */
void process_print_list(void);

#endif /* PROC_PROCESS_H */
