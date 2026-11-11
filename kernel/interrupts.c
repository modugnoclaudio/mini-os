/*
 * kernel/interrupts.c — Smistamento degli interrupt verso i gestori scritti in C.
 *
 * Percorso completo di un interrupt:
 *   1. la CPU consulta la IDT (kernel/idt.c) e salta allo stub giusto
 *   2. lo stub in assembly (kernel/interrupt_stubs.asm) salva i registri
 *   3. viene chiamata interrupt_dispatch(), qui sotto
 *   4. interrupt_dispatch() chiama il gestore registrato per quel numero
 *   5. si torna all'assembly, che ripristina i registri ed esegue `iret`
 *
 * COLLEGAMENTO CON LA TEORIA: ci sono due famiglie di interrupt.
 *   - ECCEZIONI (0-31): sincrone, le provoca l'istruzione in esecuzione
 *     (divisione per zero, page fault...). A lezione: "trap" e "fault".
 *   - INTERRUPT HARDWARE o IRQ (32-47): asincroni, li manda un dispositivo
 *     (timer, tastiera) in un momento qualsiasi.
 */

#include <stdint.h>
#include "kernel/interrupts.h"
#include "kernel/idt.h"
#include "lib/kprintf.h"

/*
 * La tabella dei gestori: un array di puntatori a funzione, indicizzato dal numero
 * dell'interrupt. Una casella a 0 (puntatore nullo) significa "nessun gestore".
 * Essendo `static` e non inizializzato, parte tutto a zero.
 */
static interrupt_handler_t handlers[INTERRUPT_STUB_COUNT];

/* Nomi delle eccezioni della CPU, per messaggi di errore leggibili.
 * È un array di stringhe: ogni elemento è un `const char *` (puntatore al primo carattere). */
static const char *exception_names[32] = {
    "Divisione per zero",            /*  0 */
    "Debug",                         /*  1 */
    "Interrupt non mascherabile",    /*  2 */
    "Breakpoint",                    /*  3 */
    "Overflow",                      /*  4 */
    "Superamento dei limiti",        /*  5 */
    "Istruzione non valida",         /*  6 */
    "Coprocessore non disponibile",  /*  7 */
    "Double fault",                  /*  8 */
    "Riservata",                     /*  9 */
    "TSS non valido",                /* 10 */
    "Segmento non presente",         /* 11 */
    "Errore sul segmento di stack",  /* 12 */
    "General protection fault",      /* 13 */
    "Page fault",                    /* 14 */
    "Riservata",                     /* 15 */
    "Errore in virgola mobile",      /* 16 */
    "Errore di allineamento",        /* 17 */
    "Machine check",                 /* 18 */
    "Errore SIMD",                   /* 19 */
    "Errore di virtualizzazione",    /* 20 */
    "Control protection",            /* 21 */
    "Riservata", "Riservata", "Riservata", "Riservata",     /* 22-25 */
    "Riservata", "Riservata", "Riservata", "Riservata",     /* 26-29 */
    "Riservata", "Riservata"                                /* 30-31 */
};

void interrupts_enable(void)
{
    /* `sti` = "set interrupt flag": da qui la CPU accetta gli interrupt hardware. */
    __asm__ volatile ("sti");
}

void interrupts_disable(void)
{
    /* `cli` = "clear interrupt flag": gli interrupt hardware restano in attesa. */
    __asm__ volatile ("cli");
}

void interrupt_register_handler(uint8_t number, interrupt_handler_t handler)
{
    if (number < INTERRUPT_STUB_COUNT) {
        handlers[number] = handler;
    }
}

/*
 * "Kernel panic": un'eccezione che nessuno sa gestire. Non possiamo continuare
 * (riprendere rieseguirebbe la stessa istruzione sbagliata, all'infinito), quindi
 * stampiamo lo stato della CPU e ci fermiamo.
 * In Java un'eccezione non catturata termina il thread con uno stack trace;
 * qui il "thread" è l'intero sistema operativo.
 */
static void panic_on_exception(struct registers *regs)
{
    /* `regs->eip` è la scorciatoia per `(*regs).eip`: accede al campo di una struct
     * attraverso un puntatore. In Java si scrive sempre `.` perché ogni oggetto è
     * già un riferimento. */
    kprintf("\n*** KERNEL PANIC: eccezione %d (%s) ***\n",
            regs->int_no, exception_names[regs->int_no]);
    kprintf("codice di errore: 0x%x\n", regs->err_code);
    kprintf("EIP=0x%x  CS=0x%x  EFLAGS=0x%x\n", regs->eip, regs->cs, regs->eflags);
    kprintf("EAX=0x%x  EBX=0x%x  ECX=0x%x  EDX=0x%x\n",
            regs->eax, regs->ebx, regs->ecx, regs->edx);
    kprintf("ESI=0x%x  EDI=0x%x  EBP=0x%x\n", regs->esi, regs->edi, regs->ebp);
    kprintf("Sistema fermo.\n");

    /* EIP è l'indirizzo dell'istruzione colpevole. Per trovarla nel sorgente:
     *     addr2line -e build/kernel.elf <valore di EIP>                          */

    interrupts_disable();
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* Gestore dell'eccezione 3 (breakpoint, istruzione `int3`). A differenza delle altre
 * non è un errore: EIP salvato punta già all'istruzione successiva, quindi basta
 * ritornare e il codice riprende. La usiamo per provare che tutto il giro funziona. */
static void breakpoint_handler(struct registers *regs)
{
    kprintf("Eccezione 3 (Breakpoint) a EIP=0x%x: gestita, si riprende\n", regs->eip);
}

/*
 * Chiamata da interrupt_common (assembly) per OGNI interrupt.
 * `regs` punta ai registri salvati sullo stack. Modificarli qui cambia i valori
 * che verranno ripristinati: è così che, nella fase 5, le syscall restituiranno
 * il risultato al programma utente (scrivendo in regs->eax).
 */
void interrupt_dispatch(struct registers *regs)
{
    interrupt_handler_t handler = handlers[regs->int_no];

    if (handler != 0) {
        /* Chiamata attraverso il puntatore a funzione: stessa sintassi di una
         * chiamata normale. */
        handler(regs);
    } else if (regs->int_no < 32) {
        /* Un'eccezione senza gestore è un bug del kernel. */
        panic_on_exception(regs);
    }
}

void interrupts_init(void)
{
    idt_init();
    interrupt_register_handler(INT_BREAKPOINT, breakpoint_handler);
}
