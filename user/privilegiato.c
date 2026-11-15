/*
 * user/privilegiato.c — Un programma che prova a eseguire un'istruzione privilegiata.
 *
 * `cli` disabilita gli interrupt. Se un programma utente potesse farlo, il timer non
 * riuscirebbe più a togliergli la CPU: un solo programma bloccherebbe l'intero
 * sistema. Per questo la CPU, in modalità utente, rifiuta l'istruzione e genera
 * l'eccezione 13 (general protection fault); il kernel termina il processo.
 *
 * COLLEGAMENTO CON LA TEORIA: è la distinzione tra modo kernel e modo utente.
 * Certe istruzioni (cli, hlt, in/out, modificare CR3...) esistono solo nel primo.
 */

#include "user/lib/ulib.h"

int main(void)
{
    print("  [privilegiato] provo a disabilitare gli interrupt con cli...\n");

    __asm__ volatile ("cli");

    /* Qui non si deve arrivare mai. */
    print("  [privilegiato] ERRORE: la CPU mi ha lasciato eseguire cli!\n");
    return 1;
}
