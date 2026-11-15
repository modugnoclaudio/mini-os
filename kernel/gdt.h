/*
 * kernel/gdt.h — La GDT (Global Descriptor Table): la tabella dei segmenti della CPU.
 * Qui ci sono solo le firme; il codice è in kernel/gdt.c.
 */
#ifndef KERNEL_GDT_H
#define KERNEL_GDT_H

/*
 * "Selettori" dei segmenti: sono i valori da caricare nei registri di segmento
 * (CS, DS, SS...) per scegliere una riga della GDT.
 * Ogni riga occupa 8 byte, quindi la riga 1 sta all'offset 8 (0x08), la riga 2 a 16 (0x10).
 */
#define GDT_KERNEL_CODE 0x08    /* riga 1: codice del kernel */
#define GDT_KERNEL_DATA 0x10    /* riga 2: dati del kernel   */

/*
 * Selettori per la modalità utente. I 2 bit più bassi di un selettore indicano il
 * livello di privilegio con cui lo si vuole usare (RPL): 3 = utente.
 * Quindi riga 3 (offset 0x18) + 3 = 0x1B, riga 4 (offset 0x20) + 3 = 0x23.
 */
#define GDT_USER_CODE   0x1B    /* riga 3: codice dei programmi utente */
#define GDT_USER_DATA   0x23    /* riga 4: dati dei programmi utente   */
#define GDT_TSS         0x28    /* riga 5: il TSS (vedi kernel/gdt.c)  */

/* Costruisce la GDT e dice alla CPU di usarla. */
void gdt_init(void);

/* Dice alla CPU quale stack del kernel usare quando arriva un interrupt mentre gira
 * codice utente. Va aggiornato a ogni cambio di processo. */
void gdt_set_kernel_stack(uint32_t stack_top);

#endif /* KERNEL_GDT_H */
