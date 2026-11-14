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
 *     EBX, ECX, EDX = primo, secondo e terzo argomento
 *     al ritorno, EAX = risultato (un valore negativo indica un errore)
 */
#ifndef KERNEL_SYSCALL_NUMBERS_H
#define KERNEL_SYSCALL_NUMBERS_H

#define SYS_EXIT    0   /* exit(codice): termina il processo. Non ritorna.            */
#define SYS_WRITE   1   /* write(testo, lunghezza): stampa; restituisce i byte scritti */
#define SYS_GETPID  2   /* getpid(): restituisce il PID del processo                  */
#define SYS_SLEEP   3   /* sleep(tick): sospende il processo per quel numero di tick  */
#define SYS_YIELD   4   /* yield(): cede la CPU agli altri processi                   */

#define SYSCALL_COUNT 5

#endif /* KERNEL_SYSCALL_NUMBERS_H */
