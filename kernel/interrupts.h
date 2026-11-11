/*
 * kernel/interrupts.h — Gestione degli interrupt lato C.
 * Il codice è in kernel/interrupts.c.
 */
#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#include <stdint.h>

/* Numeri di interrupt usati dal kernel. */
#define INT_BREAKPOINT 3        /* eccezione generata dall'istruzione `int3`       */
#define INT_IRQ_BASE   32       /* l'IRQ n arriva come interrupt numero 32 + n      */
#define IRQ_TIMER      0
#define IRQ_KEYBOARD   1

/*
 * La "fotografia" della CPU al momento dell'interrupt: tutto ciò che è stato messo
 * sullo stack dalla CPU e da kernel/interrupt_stubs.asm.
 *
 * I campi sono in ordine di indirizzo CRESCENTE, cioè al contrario dell'ordine dei
 * push (lo stack cresce verso il basso: l'ultimo valore inserito sta più in basso).
 * NON CAMBIARE L'ORDINE senza cambiare anche l'assembly.
 *
 * COLLEGAMENTO CON LA TEORIA: è il "contesto" del codice interrotto. Nella fase 4
 * salvare e ripristinare una struttura come questa sarà il context switch.
 */
struct registers {
    /* Registri di segmento, salvati da interrupt_common (valgono solo i 16 bit bassi). */
    uint32_t gs, fs, es, ds;

    /* Registri generali, salvati da `pusha`.
     * `esp_saved` è il valore di ESP prima di pusha: `popa` lo ignora. */
    uint32_t edi, esi, ebp, esp_saved, ebx, edx, ecx, eax;

    /* Messi dallo stub: numero dell'interrupt e codice di errore (0 se non previsto). */
    uint32_t int_no, err_code;

    /* Salvati automaticamente dalla CPU: dove riprendere e con quali flag. */
    uint32_t eip, cs, eflags;

    /* Presenti SOLO se l'interrupt è arrivato mentre girava codice utente (fase 5):
     * in quel caso la CPU cambia stack e salva anche quello vecchio. */
    uint32_t user_esp, user_ss;
};

/*
 * Tipo "puntatore a funzione": una variabile che contiene l'indirizzo di una funzione
 * che prende un `struct registers *` e non restituisce nulla.
 * In Java sarebbe un'interfaccia funzionale, es. `Consumer<Registers>`, a cui passare
 * un metodo con `this::metodo`. In C si passa direttamente il nome della funzione.
 */
typedef void (*interrupt_handler_t)(struct registers *regs);

/* Prepara IDT e controllore degli interrupt. Non abilita ancora gli interrupt. */
void interrupts_init(void);

/* Registra la funzione da chiamare quando arriva l'interrupt `number` (0-47). */
void interrupt_register_handler(uint8_t number, interrupt_handler_t handler);

/* Abilita / disabilita gli interrupt hardware nella CPU (istruzioni `sti` e `cli`). */
void interrupts_enable(void);
void interrupts_disable(void);

#endif /* KERNEL_INTERRUPTS_H */
