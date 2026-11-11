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

/* Costruisce la GDT e dice alla CPU di usarla. */
void gdt_init(void);

#endif /* KERNEL_GDT_H */
