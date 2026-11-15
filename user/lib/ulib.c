/*
 * user/lib/ulib.c — Libreria dei programmi utente: le syscall viste "dall'altra parte".
 *
 * Quasi ogni funzione qui sotto è un sottile involucro attorno a `int 0x80`. È
 * esattamente ciò che fa la libreria standard del C su un sistema vero: `write()`,
 * `exit()` e simili sono poche righe che caricano dei registri ed entrano nel kernel.
 */

#include <stdint.h>
#include "user/lib/ulib.h"
#include "kernel/syscall_numbers.h"     /* il "contratto" con il kernel */

/*
 * Esegue una syscall con fino a quattro argomenti e ne restituisce il risultato.
 *
 * Convenzione (decisa in kernel/syscall_numbers.h):
 *     EAX = numero, EBX/ECX/EDX/ESI = argomenti; al ritorno EAX = risultato.
 * Nell'assembly inline di GCC: "a" = EAX, "b" = EBX, "c" = ECX, "d" = EDX, "S" = ESI;
 * "=a"(result) significa "dopo l'istruzione, copia EAX in result".
 * "memory" avvisa il compilatore che il kernel potrebbe leggere o scrivere la
 * memoria che gli passiamo, quindi non deve fare ipotesi sul suo contenuto.
 *
 * Dopo `int $0x80` il programma è fermo: sta girando il kernel. Quando il kernel
 * ha finito, l'esecuzione riprende dall'istruzione successiva.
 */
static int syscall(uint32_t number, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    int result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
                      : "memory");
    return result;
}

void exit(int exit_code)
{
    syscall(SYS_EXIT, (uint32_t) exit_code, 0, 0, 0);

    /* Il kernel non torna mai da exit; il ciclo serve solo a dirlo al compilatore. */
    for (;;) {
    }
}

int write(const char *text, uint32_t length)
{
    /* Al kernel passiamo l'INDIRIZZO del testo: è un indirizzo del NOSTRO spazio di
     * memoria. Il kernel controllerà che sia davvero nostro prima di leggerlo. */
    return syscall(SYS_WRITE, (uint32_t) text, length, 0, 0);
}

int getpid(void)
{
    return syscall(SYS_GETPID, 0, 0, 0, 0);
}

void sleep(uint32_t ticks)
{
    syscall(SYS_SLEEP, ticks, 0, 0, 0);
}

void yield(void)
{
    syscall(SYS_YIELD, 0, 0, 0, 0);
}

char read_key(void)
{
    /* Syscall bloccante: se non ci sono tasti in attesa, il kernel sospende il
     * processo qui e lo riprende quando ne arriva uno. */
    return (char) syscall(SYS_READ_KEY, 0, 0, 0, 0);
}

int spawn(const char *name)
{
    return syscall(SYS_SPAWN, (uint32_t) name, 0, 0, 0);
}

int wait(int pid)
{
    return syscall(SYS_WAIT, (uint32_t) pid, 0, 0, 0);
}

void ps(void)
{
    syscall(SYS_PS, 0, 0, 0, 0);
}

int file_info(uint32_t index, char *name, uint32_t name_size)
{
    return syscall(SYS_FILE_INFO, index, (uint32_t) name, name_size, 0);
}

int file_read(const char *name, void *buffer, uint32_t size, uint32_t offset)
{
    return syscall(SYS_FILE_READ, (uint32_t) name, (uint32_t) buffer, size, offset);
}

int file_write(const char *name, const void *data, uint32_t size)
{
    return syscall(SYS_FILE_WRITE, (uint32_t) name, (uint32_t) data, size, 0);
}

int file_delete(const char *name)
{
    return syscall(SYS_FILE_DELETE, (uint32_t) name, 0, 0, 0);
}

void print(const char *text)
{
    write(text, string_length(text));
}

/* Converte un numero senza segno in caratteri (nella base indicata) e lo stampa.
 * Stesso algoritmo di print_unsigned in lib/kprintf.c: le cifre si ottengono al
 * contrario, quindi si riempie il buffer partendo dal fondo. */
static void print_unsigned(uint32_t value, uint32_t base)
{
    const char *digits = "0123456789abcdef";
    char buffer[10];                    /* 10 cifre bastano per 32 bit in base 10 */
    uint32_t position = sizeof(buffer);

    do {
        position--;
        buffer[position] = digits[value % base];
        value = value / base;
    } while (value != 0);

    /* `buffer + position` = indirizzo della prima cifra valida. */
    write(buffer + position, sizeof(buffer) - position);
}

void print_number(int value)
{
    if (value < 0) {
        print("-");
        print_unsigned(0 - (uint32_t) value, 10);   /* vedi la nota in lib/kprintf.c */
    } else {
        print_unsigned((uint32_t) value, 10);
    }
}

void print_hex(uint32_t value)
{
    print("0x");
    print_unsigned(value, 16);
}

uint32_t read_line(char *buffer, uint32_t size)
{
    uint32_t length = 0;

    for (;;) {
        char c = read_key();

        if (c == '\n') {
            /* Invio: la riga è completa. */
            write("\n", 1);
            break;
        }

        if (c == '\b') {
            /* Backspace: si toglie l'ultimo carattere, se c'è. Stampando '\b' il
             * kernel lo cancella anche dallo schermo. */
            if (length > 0) {
                length--;
                write("\b", 1);
            }
            continue;
        }

        /* Si accettano solo i caratteri stampabili (in ASCII da 32, lo spazio, a
         * 126), e solo finché c'è posto: l'ultima casella è riservata al
         * terminatore. I caratteri in più vengono semplicemente ignorati: mai
         * scrivere oltre la fine di un buffer. */
        if (c >= 32 && c <= 126 && length < size - 1) {
            buffer[length] = c;
            length++;

            /* L'ECO: è il programma, non la tastiera, a far comparire il carattere.
             * `&c` è l'indirizzo della variabile: una "stringa" di un solo carattere. */
            write(&c, 1);
        }
    }

    buffer[length] = '\0';
    return length;
}
