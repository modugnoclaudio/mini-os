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
#include "proc/programs.h"
#include "fs/ramfs.h"
#include "lib/string.h"

/* ---------------------------------------------------------------------------
 * Controllo dei dati che arrivano dai programmi
 * ---------------------------------------------------------------------------
 * Regola d'oro: un puntatore ricevuto da un programma utente è solo un NUMERO
 * scelto da lui. Prima di usarlo va verificato che indichi davvero memoria sua.
 */

/* Restituisce 1 se la zona [address, address + length) è memoria utente del
 * processo corrente. Una zona lunga zero è sempre accettata (non verrà toccata). */
static int user_range_ok(uint32_t address, uint32_t length)
{
    if (length == 0) {
        return 1;
    }
    return paging_is_user_range(current_process->directory, address, length);
}

/*
 * Copia nel kernel una stringa che sta nella memoria del programma (per esempio
 * il nome di un file). `size` è lo spazio disponibile in `destination`.
 * Restituisce 1 se riesce; 0 se l'indirizzo non è valido o se la stringa non
 * termina entro `size` caratteri.
 *
 * Perché copiarla invece di usarla dov'è? Perché di una stringa non conosciamo la
 * lunghezza finché non troviamo lo '\0': va controllato un byte alla volta che
 * ognuno sia leggibile, altrimenti una stringa senza terminatore ci farebbe
 * leggere oltre la memoria del programma.
 */
static int copy_string_from_user(char *destination, uint32_t address, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (!user_range_ok(address + i, 1)) {
            return 0;
        }

        char c = *(const char *) (address + i);
        destination[i] = c;

        if (c == '\0') {
            return 1;
        }
    }

    return 0;   /* nessun terminatore trovato: nome troppo lungo */
}

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
    if (!user_range_ok(text_address, length)) {
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

/* ---------------------------------------------------------------------------
 * Syscall per i processi
 * --------------------------------------------------------------------------- */

static int32_t sys_spawn(uint32_t name_address, uint32_t unused1, uint32_t unused2, uint32_t unused3)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;

    char name[FS_NAME_LENGTH];
    if (!copy_string_from_user(name, name_address, sizeof(name))) {
        return -1;
    }

    struct process *child = program_run(name);
    if (child == 0) {
        return -1;      /* file inesistente, non è un programma, o memoria finita */
    }

    /*
     * Registriamo chi è il genitore: servirà a wait() e alla regola degli zombie.
     * Il figlio non può essere partito nel frattempo: le syscall girano a
     * interrupt spenti, quindi nessuno ci ha tolto la CPU.
     *
     * COLLEGAMENTO CON LA TEORIA: su Unix un processo nuovo si crea in due passi,
     * fork (duplica il processo) + exec (lo sostituisce con un programma). La
     * nostra spawn li fonde in uno, come CreateProcess di Windows: molto più
     * semplice, perché non c'è da copiare lo spazio di indirizzi del genitore.
     */
    child->parent_pid = current_process->pid;

    return (int32_t) child->pid;
}

static int32_t sys_wait(uint32_t pid, uint32_t unused1, uint32_t unused2, uint32_t unused3)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;

    /* Si può attendere solo un proprio figlio: altrimenti un programma potrebbe
     * "rubare" il codice di uscita di un processo altrui. */
    struct process *child = process_find(pid);
    if (child == 0 || child == current_process || child->parent_pid != current_process->pid) {
        return -1;
    }

    /*
     * Attesa PASSIVA della fine del figlio: il processo si blocca e verrà
     * risvegliato da process_exit (o process_kill) del figlio. Il ciclo ricontrolla
     * la condizione a ogni risveglio.
     * Il PCB del figlio resta valido per tutto il tempo: finché ha un genitore
     * vivo che non ha ancora fatto wait, è uno zombie e non viene liberato.
     */
    current_process->waiting_for_pid = pid;
    while (child->state != PROCESS_TERMINATED) {
        process_block();
    }
    current_process->waiting_for_pid = 0;

    int32_t exit_code = child->exit_code;

    /* Codice letto: il figlio non ha più bisogno di restare zombie. Azzerando il
     * genitore diciamo al processo 0 che ora può liberarne la memoria. */
    child->parent_pid = 0;

    return exit_code;
}

static int32_t sys_ps(uint32_t unused1, uint32_t unused2, uint32_t unused3, uint32_t unused4)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;
    (void) unused4;

    /* Scorciatoia: è il kernel a stampare l'elenco. Un sistema vero restituirebbe
     * i dati al programma (su Linux tramite i file di /proc), lasciando a lui il
     * compito di mostrarli. */
    process_print_list();
    return 0;
}

/* ---------------------------------------------------------------------------
 * Syscall per i file
 * ---------------------------------------------------------------------------
 * Sottili involucri attorno alle funzioni di fs/ramfs.c: il loro lavoro è quasi
 * solo CONTROLLARE gli argomenti prima di passarli al filesystem.
 */

static int32_t sys_file_info(uint32_t index, uint32_t name_address, uint32_t name_size, uint32_t unused)
{
    (void) unused;

    const struct file *file = fs_get_by_index(index);
    if (file == 0) {
        return -1;      /* non ci sono altri file: chi elenca si ferma qui */
    }

    /* Stavolta è il kernel a SCRIVERE nella memoria del programma: il buffer
     * indicato deve essere tutto suo. */
    if (name_size == 0 || !user_range_ok(name_address, name_size)) {
        return -1;
    }

    string_copy((char *) name_address, file->name, name_size);
    return (int32_t) file->size;
}

static int32_t sys_file_read(uint32_t name_address, uint32_t buffer_address,
                             uint32_t size, uint32_t offset)
{
    char name[FS_NAME_LENGTH];
    if (!copy_string_from_user(name, name_address, sizeof(name))) {
        return -1;
    }

    if (size > FS_MAX_FILE_SIZE || !user_range_ok(buffer_address, size)) {
        return -1;
    }

    return fs_read(name, offset, (uint8_t *) buffer_address, size);
}

static int32_t sys_file_write(uint32_t name_address, uint32_t data_address,
                              uint32_t size, uint32_t unused)
{
    (void) unused;

    char name[FS_NAME_LENGTH];
    if (!copy_string_from_user(name, name_address, sizeof(name))) {
        return -1;
    }

    if (size > FS_MAX_FILE_SIZE || !user_range_ok(data_address, size)) {
        return -1;
    }

    /* fs_write copia i dati in memoria del kernel: dopo la syscall il programma
     * può riutilizzare o liberare il proprio buffer. */
    return fs_write(name, (const uint8_t *) data_address, size) ? 0 : -1;
}

static int32_t sys_file_delete(uint32_t name_address, uint32_t unused1, uint32_t unused2, uint32_t unused3)
{
    (void) unused1;
    (void) unused2;
    (void) unused3;

    char name[FS_NAME_LENGTH];
    if (!copy_string_from_user(name, name_address, sizeof(name))) {
        return -1;
    }

    return fs_delete(name) ? 0 : -1;
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
    [SYS_SPAWN]  = sys_spawn,
    [SYS_WAIT]   = sys_wait,
    [SYS_PS]     = sys_ps,
    [SYS_FILE_INFO]   = sys_file_info,
    [SYS_FILE_READ]   = sys_file_read,
    [SYS_FILE_WRITE]  = sys_file_write,
    [SYS_FILE_DELETE] = sys_file_delete,
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
