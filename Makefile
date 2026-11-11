# Makefile — Istruzioni per compilare e avviare il kernel.
#
# `make` è per il C quello che Maven/Gradle sono per Java, ma molto più "manuale":
# ogni regola ha la forma
#
#     bersaglio: dipendenze
#     <TAB> comando per costruire il bersaglio
#
# e make riesegue il comando solo se una dipendenza è più recente del bersaglio.
# ATTENZIONE: davanti ai comandi ci vuole un carattere TAB, non degli spazi.
#
# Comandi utili:
#   make          compila tutto e produce build/kernel.elf
#   make run      avvia il kernel in QEMU (finestra VGA + seriale nel terminale)
#   make run-nox  come run ma senza finestra (solo seriale); si esce con Ctrl+A poi X
#   make test     avvio automatico senza finestra: controlla il messaggio sulla seriale
#   make debug    avvia QEMU fermo alla prima istruzione e ci collega GDB
#   make clean    cancella tutto ciò che è stato compilato

# ----------------------------------------------------------------------------
# Strumenti
# ----------------------------------------------------------------------------
CC   := gcc
AS   := nasm
LD   := ld
QEMU := qemu-system-i386

# ----------------------------------------------------------------------------
# Opzioni
# ----------------------------------------------------------------------------
# -m32                  genera codice x86 a 32 bit (il gcc di sistema è a 64 bit)
# -ffreestanding        "non c'è un sistema operativo sotto": niente main obbligatorio,
#                       niente libreria standard data per scontata
# -nostdlib             non collegare la libreria standard del C
# -fno-pie              codice a indirizzi fissi (decisi da linker.ld), non rilocabile
# -fno-stack-protector  niente "canarini" sullo stack: richiedono supporto che non abbiamo
# -Wall -Wextra         attiva (quasi) tutti gli avvisi: in C sono preziosi
# -g                    informazioni di debug per GDB
# -O0                   nessuna ottimizzazione: il codice macchina segue il sorgente
#                       riga per riga, più facile da seguire nel debugger
# -I.                   cerca gli header a partire dalla radice: #include "drivers/vga.h"
# -MMD -MP              genera i file .d con le dipendenze dagli header (vedi in fondo)
CFLAGS  := -m32 -ffreestanding -nostdlib -fno-pie -fno-stack-protector \
           -Wall -Wextra -std=gnu11 -g -O0 -I. -MMD -MP

# -f elf32  produce un file oggetto ELF a 32 bit, lo stesso formato usato da gcc
# -g        informazioni di debug
ASFLAGS := -f elf32 -g

# -m elf_i386   collega per x86 a 32 bit
# -T linker.ld  usa il nostro script per decidere gli indirizzi
# -nostdlib     nessuna libreria di sistema
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

# ----------------------------------------------------------------------------
# File
# ----------------------------------------------------------------------------
BUILD  := build
KERNEL := $(BUILD)/kernel.elf

# -kernel         QEMU fa da bootloader Multiboot e carica direttamente il file ELF
# -serial stdio   collega la porta COM1 al terminale
# -no-reboot      in caso di errore grave QEMU si chiude invece di riavviare in loop
QEMUFLAGS := -kernel $(KERNEL) -serial stdio -no-reboot

# Cartelle con i sorgenti. Quelle delle fasi future sono già elencate: basterà
# aggiungere un file .c in una di esse e make lo compilerà da solo.
SRC_DIRS := boot kernel drivers mm proc fs lib

# $(wildcard ...) elenca i file esistenti che corrispondono al modello.
C_SOURCES   := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
ASM_SOURCES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.asm))

# Trasforma i nomi: kernel/kernel.c -> build/kernel/kernel.o
#                   boot/boot.asm   -> build/boot/boot.asm.o
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.asm,$(BUILD)/%.asm.o,$(ASM_SOURCES))

# ----------------------------------------------------------------------------
# Regole
# ----------------------------------------------------------------------------
# .PHONY: questi nomi sono comandi, non file da creare.
.PHONY: all run run-nox test debug clean

# Il primo bersaglio del file è quello predefinito: `make` equivale a `make all`.
all: $(KERNEL)

# Collegamento (linking): unisce tutti i .o in un unico eseguibile ELF.
# $@ = il bersaglio (build/kernel.elf)
$(KERNEL): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo ">>> Kernel pronto: $@"

# Compilazione di un file C. Regola "a modello": % sta per qualsiasi percorso.
# $< = la prima dipendenza (il .c)     @mkdir -p crea la cartella di destinazione
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemblaggio di un file NASM.
$(BUILD)/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Avvio normale: finestra con lo schermo VGA, seriale nel terminale.
run: $(KERNEL)
	$(QEMU) $(QEMUFLAGS)

# Avvio senza finestra: si vede solo la seriale.
# Con -nographic la seriale va già sul terminale, quindi niente -serial stdio.
# Per uscire: Ctrl+A, poi X.
run-nox: $(KERNEL)
	$(QEMU) -kernel $(KERNEL) -nographic -no-reboot

# Prova automatica: avvia QEMU senza finestra per 5 secondi, salva la seriale in un
# file e controlla che contenga il messaggio atteso.
#   -serial file:...   QEMU scrive la seriale direttamente nel file
#   -monitor none      niente console di comandi di QEMU
#   < /dev/null        QEMU non ha una tastiera da cui leggere
# Qui NON si usa -serial stdio: `timeout` avvia QEMU "in background" rispetto al
# terminale, e un processo in background che tocca il terminale viene sospeso dal
# sistema operativo (segnali SIGTTIN/SIGTTOU: il controllo dei job visto a lezione).
# Risultato: QEMU restava congelato e il test si bloccava.
# `timeout` termina QEMU (il kernel non esce mai da solo); `|| true` ignora il suo
# codice di uscita "scaduto"; `grep -q` fallisce se il testo non c'è.
test: $(KERNEL)
	@rm -f $(BUILD)/serial.log
	@timeout 5 $(QEMU) -kernel $(KERNEL) -no-reboot -display none -monitor none \
		-serial file:$(BUILD)/serial.log < /dev/null > /dev/null 2>&1 || true
	@cat $(BUILD)/serial.log
	@grep -q "Hello, kernel!" $(BUILD)/serial.log \
		&& echo ">>> TEST OK: messaggio trovato sulla seriale" \
		|| (echo ">>> TEST FALLITO: messaggio non trovato" && false)

# Debug con GDB.
#   -s  QEMU apre un server GDB sulla porta TCP 1234
#   -S  la CPU parte FERMA, in attesa del comando `continue` di GDB
# QEMU va in background (&); quando si esce da GDB lo chiudiamo con kill.
# In GDB: `c` continua, `n` riga successiva, `s` entra nella funzione,
#         `p variabile` stampa, `info registers` mostra i registri, `q` esce.
debug: $(KERNEL)
	$(QEMU) $(QEMUFLAGS) -s -S & \
	QEMU_PID=$$!; \
	gdb $(KERNEL) \
		-ex "target remote localhost:1234" \
		-ex "break kernel_main" \
		-ex "continue"; \
	kill $$QEMU_PID 2>/dev/null || true

clean:
	rm -rf $(BUILD)

# Dipendenze dagli header: gcc (con -MMD) scrive accanto a ogni .o un file .d che
# dice "questo .o dipende anche da questi .h". Includendoli, make ricompila i .c
# giusti quando si modifica un header. Il `-` iniziale ignora i file non ancora creati.
-include $(OBJECTS:.o=.d)
