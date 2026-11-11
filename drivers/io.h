/*
 * drivers/io.h — Lettura e scrittura sulle "porte di I/O" della CPU x86.
 *
 * COS'È UN HEADER (.h): in Java una classe contiene sia le firme dei metodi sia il
 * loro corpo. In C sono separati: il file .h elenca COSA esiste (le firme), il file .c
 * contiene COME è fatto. Chi vuole usare una funzione fa `#include` dell'header:
 * il preprocessore copia letteralmente il testo del .h dentro il file che lo include.
 *
 * COSA SONO LE PORTE DI I/O: oltre alla memoria, la CPU x86 ha un secondo "spazio di
 * indirizzi" di 65536 porte, usato per parlare con i dispositivi (seriale, tastiera,
 * timer...). Si usano due istruzioni assembly dedicate: `out` (scrivi) e `in` (leggi).
 * Il C non ha un modo per esprimerle, quindi usiamo l'assembly inline.
 */

/* "Include guard": evita che l'header venga copiato due volte nello stesso file
 * (darebbe errori di definizione doppia). È un idioma che troverai in ogni .h. */
#ifndef DRIVERS_IO_H
#define DRIVERS_IO_H

/* stdint.h fa parte del compilatore, non della libreria standard: si può usare anche
 * in un kernel. Definisce tipi a dimensione ESATTA: uint8_t = 8 bit senza segno,
 * uint16_t = 16 bit, uint32_t = 32 bit. In Java `int` è sempre 32 bit; in C la
 * dimensione di `int` dipende dalla piattaforma, quindi con l'hardware si usano questi. */
#include <stdint.h>

/*
 * Scrive il byte `value` sulla porta `port`.
 *
 * `static inline`: la funzione è definita nell'header e il compilatore ne copia il
 * corpo nel punto di chiamata (niente costo di una chiamata a funzione).
 *
 * Sintassi dell'assembly inline di GCC:  __asm__("istruzione" : uscite : ingressi)
 *   - GCC usa la sintassi AT&T: `outb sorgente, destinazione` (al contrario di NASM),
 *     i registri hanno il prefisso %% e il suffisso `b` significa "byte".
 *   - "a"(value) = prima di eseguire, metti `value` nel registro EAX (AL è il suo byte basso)
 *   - "d"(port)  = metti `port` nel registro EDX (DX sono i suoi 16 bit bassi)
 * L'istruzione `out` vuole proprio questi due registri: non si possono scegliere.
 */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %%al, %%dx" : : "a"(value), "d"(port));
}

/*
 * Legge un byte dalla porta `port` e lo restituisce.
 *   - "=a"(value) = dopo l'istruzione, copia il registro AL nella variabile `value`
 *
 * `volatile` dopo __asm__ dice al compilatore: "questa istruzione ha effetti
 * sull'hardware, non eliminarla e non spostarla anche se ti sembra inutile".
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(value) : "d"(port));
    return value;
}

/*
 * Piccola pausa (circa un microsecondo). Alcuni chip vecchi, come il PIC, hanno bisogno
 * di un attimo tra un comando e il successivo. Scrivere sulla porta 0x80 è il trucco
 * classico: è una porta inutilizzata (serviva ai codici diagnostici del BIOS) e
 * l'operazione di I/O in sé fa perdere il tempo necessario.
 */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif /* DRIVERS_IO_H */
