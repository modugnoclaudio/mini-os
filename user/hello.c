/*
 * user/hello.c — Il primo programma utente.
 *
 * Gira in modalità utente (ring 3), in uno spazio di indirizzi tutto suo. Non può
 * toccare lo schermo: per stampare chiede al kernel, con la syscall write
 * (nascosta dentro print).
 */

#include "user/lib/ulib.h"

int main(void)
{
    print("  [hello] Ciao dalla modalita' utente! Il mio PID e' ");
    print_number(getpid());
    print("\n");

    /* Il valore restituito diventa il codice di uscita del processo (0 = tutto bene). */
    return 0;
}
