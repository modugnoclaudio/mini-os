/*
 * kernel/autotest.c — Collaudo automatico della shell.
 *
 * IL PROBLEMA: la shell si prova scrivendo comandi sulla tastiera. Ma una prova
 * che richiede una persona non si può ripetere a ogni modifica.
 *
 * LA SOLUZIONE: un thread del kernel fa la parte dell'utente. Aspetta che la shell
 * sia ferma al prompt, poi "digita" un comando inserendo i caratteri nel buffer
 * della tastiera (keyboard_inject_char), esattamente dove li metterebbe il vero
 * interrupt. La shell non si accorge della differenza.
 *
 * Le verifiche sono di due tipi:
 *   - qui dentro si controlla lo STATO del sistema (il file è stato creato? il
 *     programma è stato fermato?);
 *   - `make test` controlla invece il TESTO comparso sulla seriale (la shell ha
 *     stampato il contenuto giusto?).
 *
 * PARAGONE CON JAVA: è un test di integrazione "end to end", come quelli che
 * pilotano un'interfaccia utente simulando clic e tasti.
 */

#include <stdint.h>
#include "kernel/autotest.h"
#include "kernel/interrupts.h"
#include "drivers/keyboard.h"
#include "drivers/io.h"
#include "proc/process.h"
#include "proc/scheduler.h"
#include "fs/ramfs.h"
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

/*
 * La shell è ferma al prompt, in attesa di un tasto?
 * La si cerca per nome ogni volta, dentro una sezione critica: il suo PCB potrebbe
 * essere stato liberato nel frattempo (dopo il comando `exit`), quindi non
 * conserviamo mai un puntatore a esso tra un controllo e l'altro.
 */
static int shell_is_at_prompt(void)
{
    uint32_t flags = interrupts_save_and_disable();

    struct process *shell = process_find_by_name("shell");
    int result = shell != 0
              && shell->state == PROCESS_BLOCKED    /* bloccata...                    */
              && shell->waiting_for_pid == 0        /* ...ma non in attesa di un figlio */
              && keyboard_buffer_is_empty();        /* e ha già letto tutto            */

    interrupts_restore(flags);
    return result;
}

/* La shell è bloccata in attesa della fine di un programma avviato con `run`? */
static int shell_is_waiting_for_program(void)
{
    uint32_t flags = interrupts_save_and_disable();

    struct process *shell = process_find_by_name("shell");
    int result = shell != 0 && shell->state == PROCESS_BLOCKED && shell->waiting_for_pid != 0;

    interrupts_restore(flags);
    return result;
}

/* Aspetta che la shell torni al prompt. Rinuncia dopo 5 secondi (500 tick), per
 * non restare bloccati per sempre se qualcosa va storto. Restituisce 1 se ci è riuscita. */
static int wait_for_prompt(void)
{
    for (int i = 0; i < 500; i++) {
        if (shell_is_at_prompt()) {
            return 1;
        }
        process_sleep(1);
    }
    return 0;
}

/* "Digita" una riga di comando nella shell e aspetta che abbia finito di eseguirla. */
static void type_command(const char *command)
{
    if (!wait_for_prompt()) {
        check("la shell e' pronta a ricevere un comando", 0);
        return;
    }

    for (uint32_t i = 0; command[i] != '\0'; i++) {
        keyboard_inject_char(command[i]);
    }
    keyboard_inject_char('\n');     /* il tasto Invio */

    /* Diamo alla shell il tempo di accorgersi dei tasti prima di controllare se è
     * di nuovo al prompt (altrimenti la troveremmo ancora ferma su quello vecchio). */
    process_sleep(2);
    wait_for_prompt();
}

/*
 * Spegne la macchina virtuale. Funziona solo in QEMU: scrivere il valore 0x2000
 * sulla porta 0x604 è il modo in cui QEMU espone lo spegnimento ACPI.
 * Serve a far terminare `make test` appena finite le prove, invece di aspettare
 * un tempo fisso. Su un PC vero non avrebbe effetto: lì lo spegnimento richiede di
 * leggere le tabelle ACPI, cosa che non facciamo.
 */
static void power_off(void)
{
    outw(0x604, 0x2000);
}

/* Il "main" del processo collaudatore. */
static void autotest_thread(void)
{
    kprintf("\nCollaudo automatico della shell:\n");

    check("la shell e' partita ed e' ferma al prompt", wait_for_prompt());

    uint32_t files_before = fs_count();

    /* --- Comandi che leggono soltanto: il loro output lo controlla `make test` --- */
    type_command("help");
    type_command("ls");
    type_command("cat benvenuto.txt");
    type_command("run hello");

    /* --- Creazione e cancellazione di un file --- */
    type_command("scrivi nota.txt ciao dal collaudo");
    const struct file *note = fs_find("nota.txt");
    /* 17 caratteri di testo più l'a-capo aggiunto dalla shell = 18 byte. */
    check("'scrivi' ha creato il file con il contenuto giusto",
          note != 0 && note->size == 18 && fs_count() == files_before + 1);

    type_command("cat nota.txt");

    type_command("rm nota.txt");
    check("'rm' ha cancellato il file",
          fs_find("nota.txt") == 0 && fs_count() == files_before);

    /* --- Situazioni di errore: la shell deve dare un messaggio e proseguire --- */
    type_command("cat nota.txt");               /* file che non esiste più    */
    type_command("run leggimi.txt");            /* un file di testo           */
    type_command("frullatore");                 /* comando inesistente        */
    type_command("run invasore");               /* programma che viene ucciso */
    check("dopo un programma terminato dal kernel la shell e' ancora al prompt",
          wait_for_prompt());

    type_command("ps");

    /* --- Ctrl+C su un programma che non termina mai --- */
    wait_for_prompt();
    const char *command = "run ciclo";
    for (uint32_t i = 0; command[i] != '\0'; i++) {
        keyboard_inject_char(command[i]);
    }
    keyboard_inject_char('\n');

    process_sleep(20);      /* lasciamo girare 'ciclo' per un po' */
    check("con 'run ciclo' la shell resta in attesa del programma",
          shell_is_waiting_for_program());

    keyboard_inject_ctrl_c();
    check("Ctrl+C ferma il programma e la shell torna al prompt", wait_for_prompt());

    /* --- Pulizia: a questo punto devono restare idle, shell e questo processo --- */
    process_sleep(5);       /* il processo 0 ha bisogno di un attimo per fare pulizia */
    check("non sono rimasti processi in sospeso", process_count() == 3);

    /* --- exit: la shell termina e il kernel ne avvia subito un'altra --- */
    uint32_t flags = interrupts_save_and_disable();
    struct process *shell = process_find_by_name("shell");
    uint32_t old_shell_pid = shell != 0 ? shell->pid : 0;
    interrupts_restore(flags);

    type_command("exit");

    flags = interrupts_save_and_disable();
    shell = process_find_by_name("shell");
    uint32_t new_shell_pid = shell != 0 ? shell->pid : 0;
    interrupts_restore(flags);

    check("dopo 'exit' il kernel ha avviato una nuova shell",
          new_shell_pid != 0 && new_shell_pid != old_shell_pid);

    if (failures == 0) {
        kprintf("\nShell: tutte le prove superate\n");
    } else {
        kprintf("\nShell: %d prove FALLITE\n", failures);
    }

    kprintf("Collaudo terminato: spengo la macchina virtuale.\n");
    power_off();
}

void autotest_start(void)
{
    process_create("collaudo", autotest_thread);
}
