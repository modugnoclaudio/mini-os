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
#   make test     avvio automatico senza finestra, in modalità di collaudo: esegue
#                 tutte le prove e controlla ciò che compare sulla seriale
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
# Programmi utente
# ----------------------------------------------------------------------------
# I programmi in user/*.c NON fanno parte del kernel (infatti `user` non è in
# SRC_DIRS). Ognuno diventa un file a sé, build/user/NOME.bin: un "binario piatto"
# collegato con user/user.ld per girare all'indirizzo virtuale 0x40000000.
# Il kernel li incorpora poi come dati (fs/initial_files.asm): all'avvio diventano
# file del filesystem in RAM, da cui vengono caricati in processi separati.
USER_SOURCES := $(wildcard user/*.c)
USER_BINS    := $(patsubst user/%.c,$(BUILD)/user/%.bin,$(USER_SOURCES))

# La libreria comune a tutti i programmi. crt0 va per primo: contiene _start.
# lib/string.o è lo stesso file oggetto usato dal kernel: sono funzioni di puro
# calcolo (memcpy, string_equals...), che ogni programma si porta dentro in copia.
USER_LIB_OBJECTS := $(BUILD)/user/lib/crt0.asm.o $(BUILD)/user/lib/ulib.o $(BUILD)/lib/string.o

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

# Collegamento di un programma utente: il suo .o più la libreria, con lo script
# user/user.ld. `--oformat binary` produce il binario piatto invece di un file ELF.
# (I .o si ottengono con le stesse regole di compilazione del kernel, qui sotto.)
$(BUILD)/user/%.bin: $(BUILD)/user/%.o $(USER_LIB_OBJECTS) user/user.ld
	$(LD) -m elf_i386 -T user/user.ld --oformat binary -nostdlib -o $@ $(USER_LIB_OBJECTS) $<

# Il file che incorpora nel kernel i file iniziali del filesystem (programmi utente
# e file di fs/root/) va riassemblato ogni volta che uno di essi cambia: lo diciamo
# a make con una dipendenza esplicita.
$(BUILD)/fs/initial_files.asm.o: $(USER_BINS) $(wildcard fs/root/*)

# make cancellerebbe da solo i file "intermedi" (i .o dei programmi utente) dopo
# averli usati; questa riga glielo vieta, così non vengono ricompilati ogni volta.
.SECONDARY:

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

# Prova automatica: avvia QEMU senza finestra in MODALITÀ DI COLLAUDO, salva la
# seriale in un file e controlla che contenga i messaggi attesi e che non compaia
# mai la parola "ERRORE".
#   -append autotest   la "riga di comando" passata al kernel: gli dice di eseguire
#                      le prove automatiche e di collaudare la shell (kernel/kernel.c)
#   -serial file:...   QEMU scrive la seriale direttamente nel file
#   -monitor none      niente console di comandi di QEMU
#   < /dev/null        QEMU non ha una tastiera da cui leggere
# Qui NON si usa -serial stdio: `timeout` avvia QEMU "in background" rispetto al
# terminale, e un processo in background che tocca il terminale viene sospeso dal
# sistema operativo (segnali SIGTTIN/SIGTTOU: il controllo dei job visto a lezione).
# Risultato: QEMU restava congelato e il test si bloccava.
# Finite le prove il kernel spegne da solo la macchina virtuale, quindi di norma
# QEMU termina in pochi secondi; `timeout 30` è solo una rete di sicurezza nel caso
# qualcosa si blocchi. `|| true` ignora il codice di uscita; `grep -q` fallisce se
# il testo non c'è.
test: $(KERNEL)
	@rm -f $(BUILD)/serial.log
	@timeout 30 $(QEMU) -kernel $(KERNEL) -append autotest -no-reboot -display none \
		-monitor none -serial file:$(BUILD)/serial.log < /dev/null > /dev/null 2>&1 || true
	@cat $(BUILD)/serial.log
	@grep -q "Hello, kernel!" $(BUILD)/serial.log \
		&& grep -q "Ritorno dall'eccezione riuscito" $(BUILD)/serial.log \
		&& grep -q "Memoria: tutte le prove superate" $(BUILD)/serial.log \
		&& grep -q "File system: tutte le prove superate" $(BUILD)/serial.log \
		&& grep -q "Processi: tutte le prove superate" $(BUILD)/serial.log \
		&& grep -q "Shell: tutte le prove superate" $(BUILD)/serial.log \
		&& grep -q "Scrivi 'help' per vedere i comandi disponibili" $(BUILD)/serial.log \
		&& grep -q "\[hello\] Ciao dalla modalita' utente" $(BUILD)/serial.log \
		&& grep -q "^ciao dal collaudo" $(BUILD)/serial.log \
		&& grep -q "cat: file non trovato: nota.txt" $(BUILD)/serial.log \
		&& grep -q "non esiste oppure non e' un programma" $(BUILD)/serial.log \
		&& grep -q "Comando sconosciuto: frullatore" $(BUILD)/serial.log \
		&& grep -q "^\^C" $(BUILD)/serial.log \
		&& ! grep -q "ERRORE" $(BUILD)/serial.log \
		&& echo ">>> TEST OK: kernel, memoria, filesystem, processi, programmi utente e shell funzionano" \
		|| (echo ">>> TEST FALLITO: manca un messaggio atteso oppure c'e' un ERRORE" && false)

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
-include $(OBJECTS:.o=.d) $(USER_BINS:.bin=.d) $(USER_LIB_OBJECTS:.o=.d)
