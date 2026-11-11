/*
 * drivers/pic.h — Driver del PIC 8259, il controllore degli interrupt hardware.
 * Il codice è in drivers/pic.c.
 */
#ifndef DRIVERS_PIC_H
#define DRIVERS_PIC_H

#include <stdint.h>

/* Sposta gli IRQ sugli interrupt 32-47 e li maschera (disattiva) tutti. */
void pic_init(void);

/* Attiva la linea `irq` (0-15): da quel momento il dispositivo può interrompere. */
void pic_enable_irq(uint8_t irq);

/* Comunica al PIC che l'interrupt `irq` è stato preso in carico ("End Of Interrupt"). */
void pic_send_eoi(uint8_t irq);

#endif /* DRIVERS_PIC_H */
