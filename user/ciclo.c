/*
 * user/ciclo.c — Un programma che non finisce mai e non cede mai la CPU.
 *
 * Non fa syscall, non dorme: gira e basta. Serve a dimostrare che la PRELAZIONE
 * funziona anche in modalità utente: il timer interrompe il programma, la CPU
 * passa allo stack del kernel indicato nel TSS, e lo scheduler dà la CPU a
 * qualcun altro. Un programma, per quanto maleducato, non può bloccare il sistema.
 *
 * Non terminando mai da solo, deve essere il kernel a eliminarlo (process_kill).
 */

int main(void)
{
    /* `volatile` impedisce al compilatore di eliminare un ciclo "inutile". */
    volatile unsigned int counter = 0;

    for (;;) {
        counter++;
    }

    return 0;   /* mai raggiunto */
}
