/*
 * user/conta.c — Dimostra che ogni processo ha la sua memoria.
 *
 * Il kernel avvia DUE copie di questo programma contemporaneamente. Entrambe hanno
 * la variabile globale `contatore` allo STESSO indirizzo virtuale (il programma è
 * lo stesso, collegato allo stesso indirizzo). Eppure ciascuna vede solo il proprio
 * valore: dietro quell'indirizzo ci sono due pagine fisiche diverse.
 *
 * Con i thread della fase 4, che condividevano la memoria, le due copie si
 * sarebbero pestate i piedi sulla stessa variabile.
 */

#include "user/lib/ulib.h"

/* Variabile globale: vive nella sezione .data/.bss del programma, a un indirizzo
 * fisso deciso dal linker (poco sopra 0x40000000). */
int contatore = 0;

int main(void)
{
    int pid = getpid();

    for (int i = 0; i < 3; i++) {
        /* Ogni processo somma il PROPRIO pid: alla fine deve valere 3 * pid.
         * Se la memoria fosse condivisa, i due conti si mescolerebbero. */
        contatore += pid;

        print("  [conta, pid ");
        print_number(pid);
        print("] contatore = ");
        print_number(contatore);
        print(" all'indirizzo ");
        print_hex((uint32_t) &contatore);       /* &contatore = l'indirizzo della variabile */
        print("\n");

        sleep(10);      /* un decimo di secondo: lascia girare l'altra copia */
    }

    /* Il codice di uscita è il valore finale: il kernel lo userà per la verifica. */
    return contatore;
}
