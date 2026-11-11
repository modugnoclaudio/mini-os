/*
 * kernel/idt.h — La IDT (Interrupt Descriptor Table): per ogni numero di interrupt,
 * l'indirizzo del codice da eseguire. Il codice è in kernel/idt.c.
 */
#ifndef KERNEL_IDT_H
#define KERNEL_IDT_H

/* Quanti interrupt gestiamo: 32 eccezioni della CPU + 16 interrupt hardware (IRQ). */
#define INTERRUPT_STUB_COUNT 48

/* Costruisce la IDT e dice alla CPU di usarla. Non abilita gli interrupt. */
void idt_init(void);

#endif /* KERNEL_IDT_H */
