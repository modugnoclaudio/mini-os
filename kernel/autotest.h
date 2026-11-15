/*
 * kernel/autotest.h — Collaudo automatico della shell.
 * Il codice è in kernel/autotest.c.
 *
 * Si attiva solo se il kernel viene avviato con la parola "autotest" sulla riga di
 * comando (lo fa `make test`). In un avvio normale (`make run`) non succede nulla
 * di tutto questo: si arriva direttamente alla shell.
 */
#ifndef KERNEL_AUTOTEST_H
#define KERNEL_AUTOTEST_H

/* Avvia il processo che collauda la shell "digitando" una serie di comandi.
 * Al termine stampa il riepilogo e spegne la macchina virtuale. */
void autotest_start(void);

#endif /* KERNEL_AUTOTEST_H */
