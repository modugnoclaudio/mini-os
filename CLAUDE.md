# CLAUDE.md — Istruzioni per lavorare su questo progetto

Questo file viene letto automaticamente da Claude Code a ogni sessione.
Contiene le regole del progetto: seguile sempre.

## Il progetto

Un sistema operativo didattico per x86 a 32 bit, scritto in C e un po' di assembly,
che gira in QEMU (e, alla fine, nel browser con v86).
Obiettivo: arrivare a un kernel con interrupt, memoria virtuale, processi,
syscall, un filesystem semplice e una shell.

## Chi sta imparando

L'autore è uno studente di Ingegneria Informatica:
- conosce bene **Java**, è alle prime armi con **C** e assembly;
- ha studiato **Sistemi Operativi** e conosce la teoria (scheduling, paginazione, syscall...).

Quindi:
- Scrivi codice **chiaro prima che furbo**: niente trucchi illeggibili se esiste una versione semplice.
- **Commenta molto**, soprattutto puntatori, cast, aritmetica sugli indirizzi, `volatile`, assembly inline.
- Quando un concetto C non esiste in Java (puntatori, gestione manuale della memoria,
  header, linker, undefined behavior), spiegalo in una riga con il paragone con Java.
- Collega il codice alla teoria di Sistemi Operativi quando è naturale
  (es. "questo è il context switch che si studia a lezione").

## Flusso di lavoro

**A inizio sessione**
1. Leggi `PLAN.md` per capire a che punto siamo.
2. Leggi le NOTES della fase in corso (`docs/notes/`).
3. Riparti dal "Prossimo passo" indicato in `PLAN.md`.

**Durante il lavoro**
- Procedi a piccoli passi: ogni passo deve **compilare e avviarsi in QEMU** prima di andare avanti.
- Fai un commit per ogni passo funzionante, con un messaggio chiaro in italiano.
- Se prendi una decisione di progetto (layout di memoria, formato di una struttura...),
  annotala nel "Registro delle decisioni" di `PLAN.md`.

**A fine fase (o a fine sessione)**
1. Scrivi o aggiorna `docs/notes/faseN-nome.md` seguendo **esattamente** `docs/NOTES_TEMPLATE.md`.
2. Aggiorna `PLAN.md`: stato delle fasi, problemi noti, prossimo passo.
3. Commit finale.

## Regole tecniche

- Toolchain: `gcc -m32 -ffreestanding -nostdlib`, `nasm`, `ld -m elf_i386`, `make`.
- Avvio: `qemu-system-i386 -kernel build/kernel.elf` (Multiboot 1, nessun ISO necessario all'inizio).
- Debug: `make debug` avvia QEMU con `-s -S` e si collega GDB.
- Niente libreria standard del C: ciò che serve (es. `memset`, `printf` del kernel) lo scriviamo noi in `lib/`.
- Output di debug anche sulla porta seriale (`-serial stdio`), così i log si leggono nel terminale.
