/*
 * kernel/syscall_numbers.h — I numeri delle chiamate di sistema.
 *
 * È L'UNICO file condiviso tra il kernel e i programmi utente (che per il resto
 * sono compilati separatamente e non possono chiamare nessuna funzione del kernel).
 * È il "contratto" tra i due mondi: il programma mette uno di questi numeri nel
 * registro EAX ed esegue `int 0x80`; il kernel guarda EAX per capire cosa gli si chiede.
 *
 * Convenzione di chiamata (decisa da noi, uguale a quella di Linux a 32 bit):
 *     EAX = numero della syscall
 *     EBX, ECX, EDX, ESI = primo, secondo, terzo e quarto argomento
 *     al ritorno, EAX = risultato (un valore negativo indica un errore)
 */
#ifndef KERNEL_SYSCALL_NUMBERS_H
#define KERNEL_SYSCALL_NUMBERS_H

#define SYS_EXIT    0   /* exit(codice): termina il processo. Non ritorna.            */
#define SYS_WRITE   1   /* write(testo, lunghezza): stampa; restituisce i byte scritti */
#define SYS_GETPID  2   /* getpid(): restituisce il PID del processo                  */
#define SYS_SLEEP   3   /* sleep(tick): sospende il processo per quel numero di tick  */
#define SYS_YIELD   4   /* yield(): cede la CPU agli altri processi                   */
#define SYS_READ_KEY 5  /* read_key(): attende un tasto e ne restituisce il carattere */

/* Processi */
#define SYS_SPAWN   6   /* spawn(nome): avvia il programma contenuto in quel file;
                           restituisce il PID del nuovo processo, -1 se non riesce    */
#define SYS_WAIT    7   /* wait(pid): attende la fine di un processo avviato con spawn
                           e ne restituisce il codice di uscita                       */
#define SYS_PS      8   /* ps(): stampa l'elenco dei processi                         */

/* File */
#define SYS_FILE_INFO   9   /* file_info(indice, buffer_nome, dimensione_buffer): copia nel
                               buffer il nome del file numero `indice` e ne restituisce
                               la dimensione; -1 se l'indice supera il numero di file  */
#define SYS_FILE_READ   10  /* file_read(nome, buffer, quanti, da_dove): legge una parte
                               del file; restituisce i byte letti, -1 se non esiste    */
#define SYS_FILE_WRITE  11  /* file_write(nome, dati, quanti): crea o sostituisce il file;
                               0 se riesce, -1 altrimenti                              */
#define SYS_FILE_DELETE 12  /* file_delete(nome): cancella il file; 0 se riesce, -1 se no */

#define SYSCALL_COUNT 13

#endif /* KERNEL_SYSCALL_NUMBERS_H */
