/*
 * user/invasore.c — Un programma "cattivo" che prova a toccare la memoria del kernel.
 *
 * Serve a dimostrare la PROTEZIONE DELLA MEMORIA: il kernel è mappato anche nello
 * spazio di indirizzi di questo processo, ma le sue pagine non hanno il permesso
 * "utente". Qualsiasi tentativo di accesso viene fermato dalla CPU.
 *
 * Fa due tentativi, uno più furbo dell'altro.
 */

#include "user/lib/ulib.h"

/* L'indirizzo a cui è caricato il kernel (1 MiB, vedi linker.ld). */
#define KERNEL_ADDRESS 0x100000

int main(void)
{
    /*
     * TENTATIVO 1 (furbo): non tocco io la memoria del kernel, chiedo al KERNEL di
     * leggerla per me, passando quell'indirizzo a write(). Se il kernel si fidasse
     * dei puntatori che riceve, stamperebbe i propri dati segreti.
     * Il kernel invece controlla e risponde -1.
     */
    int result = write((const char *) KERNEL_ADDRESS, 16);
    if (result == -1) {
        print("  [invasore] write() con un indirizzo del kernel: rifiutata\n");
    } else {
        print("  [invasore] ERRORE: il kernel ha accettato un indirizzo non mio!\n");
    }

    /*
     * TENTATIVO 2 (diretto): scrivo direttamente a quell'indirizzo.
     * La MMU vede che la pagina non è accessibile in modalità utente e genera un
     * page fault. Il kernel, invece di andare in panic come farebbe per un proprio
     * errore, termina solo questo processo.
     * In Java non esiste niente di simile perché non esistono puntatori liberi:
     * qui la protezione la fa l'hardware.
     */
    print("  [invasore] ora provo a scrivere direttamente nel kernel...\n");

    volatile int *kernel_memory = (volatile int *) KERNEL_ADDRESS;
    *kernel_memory = 0;

    /* Qui non si deve arrivare mai. */
    print("  [invasore] ERRORE: sono riuscito a scrivere nel kernel!\n");
    return 1;
}
