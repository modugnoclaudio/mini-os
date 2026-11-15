/*
 * kernel/syscall.c — Le chiamate di sistema (syscall).
 *
 * IL PROBLEMA: un programma utente gira con la CPU in modalità non privilegiata e
 * vede solo la propria memoria. Non può toccare lo schermo, il timer, gli altri
 * processi. Come fa allora a stampare una riga o a terminare? Deve CHIEDERLO al
 * kernel. Ma non può nemmeno chiamare una funzione del kernel: non ne vede il codice.
 *
 * LA SOLUZIONE: un interrupt provocato apposta. Il programma mette il numero del
 * servizio in EAX, gli argomenti in EBX, ECX, EDX, ed esegue `int 0x80`. La CPU:
 *   1. passa in modalità kernel (ring 0);
 *   2. cambia stack, prendendo quello del kernel dal TSS (kernel/gdt.c);
 *   3. salta al gestore scritto nella riga 0x80 della IDT.
 * Si entra quindi nel kernel solo da UN punto, scelto dal kernel, che controlla ogni
 * richiesta prima di esaudirla. Al termine, `iret` riporta in modalità utente.
 *
 * COLLEGAMENTO CON LA TEORIA: è il meccanismo delle "system call" (o "trap"): il
 * passaggio controllato da modo utente a modo kernel.
 *
 * PARAGONE CON JAVA: quando scrivi System.out.println(), dopo molti strati la JVM
 * arriva a fare una syscall `write` al sistema operativo. Qui scriviamo l'altro
 * capo di quella chiamata.
 */

#include <stdint.h>
#include "kernel/syscall.h"
#include "kernel/syscall_numbers.h"
#include "kernel/interrupts.h"
#include "kernel/idt.h"
#include "proc/process.h"
#include "proc/scheduler.h"
#include "mm/paging.h"
#include "lib/kprintf.h"
#include "drivers/keyboard.h"

/*
 * Ogni syscall è una normale funzione C con al massimo quattro argomenti interi.
 * Hanno tutte la stessa firma, così possono stare in un'unica tabella; chi non usa
 * un argomento lo ignora con il cast a (void), che evita l'avviso del compilatore.
 */

static int32_t sys_exit(uint32_t exit_code, uint32_t unused1, uint32_t unused2, uint32_t unused3)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;

    process_exit((int32_t) exit_code);
    return 0;       /* mai raggiunto: process_exit non ritorna */
}

/* Il massimo che una singola write può stampare. La syscall gira a interrupt
 * spenti: una stampa enorme bloccherebbe l'intero sistema. */
#define WRITE_MAX_LENGTH 4096

static int32_t sys_write(uint32_t text_address, uint32_t length, uint32_t unused1, uint32_t unused2)
{
    (void) unused1;
    (void) unused2;

    if (length > WRITE_MAX_LENGTH) {
        return -1;
    }

    /*
     * IL CONTROLLO PIÙ IMPORTANTE DI TUTTO IL FILE. `text_address` è un numero
     * scelto dal programma. Il kernel può leggere QUALSIASI indirizzo: se si
     * fidasse, un programma potrebbe passare l'indirizzo di dati del kernel (o
     * di un indirizzo non mappato, per farlo andare in panic) e farseli stampare.
     * Verifichiamo quindi che tutta la zona appartenga davvero allo spazio utente
     * di QUESTO processo. (Vedi user/invasore.c, che ci prova.)
     */
    if (!paging_is_user_range(current_process->directory, text_address, length)) {
        return -1;
    }

    /*
     * Ora possiamo usare l'indirizzo del programma direttamente: durante una
     * syscall è ancora attivo lo spazio di indirizzi del processo che l'ha fatta,
     * quindi i suoi indirizzi virtuali sono validi anche per il kernel.
     */
    const char *text = (const char *) text_address;
    for (uint32_t i = 0; i < length; i++) {
        kputchar(text[i]);
    }

    return (int32_t) length;
}

static int32_t sys_read_key(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;
    (void) unused4;

    /*
     * Una syscall BLOCCANTE: se non c'è nessun tasto in attesa, il processo viene
     * sospeso qui dentro, a metà della syscall, e la CPU passa ad altri. Quando
     * l'utente premerà un tasto il processo riprenderà da questo punto e la
     * syscall ritornerà. Dal punto di vista del programma, la chiamata ha
     * semplicemente "impiegato del tempo".
     */
    return (int32_t) (uint8_t) keyboard_read_char();
}

static int32_t sys_getpid(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;
    (void) unused4;

    return (int32_t) current_process->pid;
}

static int32_t sys_sleep(uint32_t ticks, uint32_t unused1, uint32_t unused2, uint32_t unused3)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;

    process_sleep(ticks);
    return 0;
}

static int32_t sys_yield(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;
    (void) unused4;

    process_yield();
    return 0;
}

/*
 * La tabella delle syscall: un array di puntatori a funzione indicizzato dal numero
 * della syscall. `typedef` dà un nome al tipo "puntatore a funzione con questa firma".
 *
 * La sintassi `[SYS_EXIT] = sys_exit` ("inizializzatore designato") assegna ogni
 * funzione alla casella con quell'indice: così l'ordine delle righe non conta e non
 * si può sbagliare l'abbinamento numero-funzione.
 */
typedef int32_t (*syscall_function_t)(uint32_t, uint32_t, uint32_t, uint32_t);

static syscall_function_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT]   = sys_exit,
    [SYS_WRITE]  = sys_write,
    [SYS_GETPID] = sys_getpid,
    [SYS_SLEEP]  = sys_sleep,
    [SYS_YIELD]  = sys_yield,
    [SYS_READ_KEY] = sys_read_key,
};

/*
 * Il gestore dell'interrupt 0x80. `regs` contiene i registri del programma al
 * momento della `int 0x80`: è da lì che leggiamo numero e argomenti, ed è lì che
 * scriviamo il risultato.
 */
static void syscall_handler(struct registers *regs)
{
    uint32_t number = regs->eax;

    /*
     * MAI FIDARSI DI CIÒ CHE ARRIVA DA UN PROGRAMMA UTENTE. Il numero è un valore
     * qualsiasi scelto dal programma: senza questo controllo, un numero fuori
     * dall'array farebbe saltare il kernel a un indirizzo a caso.
     */
    if (number >= SYSCALL_COUNT || syscall_table[number] == 0) {
        regs->eax = (uint32_t) -1;
        return;
    }

    int32_t result = syscall_table[number](regs->ebx, regs->ecx, regs->edx, regs->esi);

    /*
     * Il valore di ritorno: lo scriviamo nella copia salvata di EAX. Quando
     * interrupt_common farà `popa`, quel valore finirà nel registro EAX vero, e il
     * programma lo troverà lì subito dopo la sua `int 0x80`.
     */
    regs->eax = (uint32_t) result;
}

void syscall_init(void)
{
    interrupt_register_handler(INT_SYSCALL, syscall_handler);
}
