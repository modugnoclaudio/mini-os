/*
 * user/lib/ulib.c — Libreria dei programmi utente: le syscall viste "dall'altra parte".
 *
 * Ogni funzione qui sotto è un sottile involucro attorno a `int 0x80`. È
 * esattamente ciò che fa la libreria standard del C su un sistema vero: `write()`,
 * `exit()` e simili sono poche righe che caricano dei registri ed entrano nel kernel.
 */

#include <stdint.h>
#include "user/lib/ulib.h"
#include "kernel/syscall_numbers.h"     /* l'unico file condiviso con il kernel */

/*
 * Esegue una syscall con fino a tre argomenti e ne restituisce il risultato.
 *
 * Convenzione (decisa in kernel/syscall_numbers.h):
 *     EAX = numero, EBX/ECX/EDX = argomenti; al ritorno EAX = risultato.
 * Nell'assembly inline di GCC: "a" = EAX, "b" = EBX, "c" = ECX, "d" = EDX;
 * "=a"(result) significa "dopo l'istruzione, copia EAX in result".
 * "memory" avvisa il compilatore che il kernel potrebbe leggere la memoria che gli
 * passiamo, quindi deve averla davvero scritta prima dell'interrupt.
 *
 * Dopo `int $0x80` il programma è fermo: sta girando il kernel. Quando il kernel
 * ha finito, l'esecuzione riprende dall'istruzione successiva.
 */
static int syscall(uint32_t number, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    int result;
    __asm__ volatile ("int $0x80"
                      : "=a"(result)
                      : "a"(number), "b"(arg1), "c"(arg2), "d"(arg3)
                      : "memory");
    return result;
}

void exit(int exit_code)
{
    syscall(SYS_EXIT, (uint32_t) exit_code, 0, 0);

    /* Il kernel non torna mai da exit; il ciclo serve solo a dirlo al compilatore. */
    for (;;) {
    }
}

int write(const char *text, uint32_t length)
{
    /* Al kernel passiamo l'INDIRIZZO del testo: è un indirizzo del NOSTRO spazio di
     * memoria. Il kernel controllerà che sia davvero nostro prima di leggerlo. */
    return syscall(SYS_WRITE, (uint32_t) text, length, 0);
}

int getpid(void)
{
    return syscall(SYS_GETPID, 0, 0, 0);
}

void sleep(uint32_t ticks)
{
    syscall(SYS_SLEEP, ticks, 0, 0);
}

void yield(void)
{
    syscall(SYS_YIELD, 0, 0, 0);
}

/* Lunghezza di una stringa: conta i caratteri fino al terminatore '\0'. */
static uint32_t string_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
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
