# PLAN.md — Stato del progetto

> Questo file è l'**indice** del progetto, non l'unica memoria.
> I dettagli stanno nel codice, nella cronologia git e nelle NOTES di ogni fase.
> Va aggiornato a fine di ogni sessione. Le sezioni "Registro delle decisioni"
> e "Problemi noti" si **aggiungono** e non si cancellano mai: al massimo si segna una voce come superata.

## Obiettivo
Sistema operativo didattico x86 a 32 bit in C, avviabile in QEMU e nel browser (v86).

Scadenza Fable: **martedì 17 novembre, ore 9:00** (ora italiana).
Dopo quella data si continua con Opus 5.5.

## Fasi

| # | Fase | Giorno previsto | Stato |
|---|---|---|---|
| 0 | Ambiente: WSL2, gcc, nasm, QEMU, GDB, Makefile, repo | mer 11 | ✅ fatta |
| 1 | Boot Multiboot, output VGA e seriale, `kprintf` | mer 11 | ✅ fatta |
| 2 | GDT, IDT, eccezioni, PIC, timer, tastiera | gio 12 | ⬜ da fare |
| 3 | Memoria: allocatore fisico (bitmap), paginazione, heap `kmalloc` | ven 13 | ⬜ da fare |
| 4 | Processi: PCB, context switch, scheduler round-robin | sab 14 | ⬜ da fare |
| 5 | Syscall (`int 0x80`) e programmi in user mode | dom 15 | ⬜ da fare |
| 6 | Filesystem in RAM e shell (`ls`, `cat`, `ps`, `run`) | lun 16 | ⬜ da fare |
| 7 | Demo nel browser con v86, README, rifiniture | mar 17+ (anche con Opus) | ⬜ da fare |

Legenda: ⬜ da fare · 🟨 in corso · ✅ fatta · ⚠️ fatta con problemi noti

## Prossimo passo
Fase 2: GDT, IDT, eccezioni, PIC, timer, tastiera.
Primo passo concreto: scrivere la nostra GDT (segmenti codice e dati "piatti" del kernel)
e caricarla con `lgdt`; poi l'IDT con i gestori delle eccezioni 0-31.

Prima di iniziare, sul PC (WSL2): verificare che `make test` e `make run` funzionino
(vedi "Come provarlo" in `docs/notes/fase1-boot.md`).

## Struttura del repository
```
CLAUDE.md          regole per Claude
PLAN.md            questo file
Makefile
linker.ld
boot/              codice di avvio in assembly
kernel/            cuore del kernel
drivers/           VGA, seriale, tastiera, timer
mm/                gestione della memoria
proc/              processi e scheduler
fs/                filesystem
user/              programmi utente e shell
lib/               funzioni di supporto (memset, kprintf...)
docs/
  NOTES_TEMPLATE.md
  notes/           una NOTES per fase
```

## Registro delle decisioni
| Data | Decisione | Motivo |
|---|---|---|
| 11/11 | x86 a 32 bit invece di 64 bit o RISC-V | Più documentazione (wiki OSDev), avvio più semplice, compatibile con v86 per la demo web |
| 11/11 | Avvio con `qemu-system-i386 -kernel` (Multiboot 1) | Niente bootloader da scrivere né ISO da creare all'inizio |
| 11/11 | `gcc -m32` di sistema invece di un cross-compiler | Si installa in un minuto con apt; il cross-compiler si valuta solo se servirà |
| 11/11 | Kernel caricato a 1 MiB, sezioni allineate a 4 KiB (`linker.ld`) | Scelta standard Multiboot; l'allineamento a pagina servirà per la paginazione |
| 11/11 | Stack del kernel di 16 KiB nella `.bss` (`boot/boot.asm`) | Abbondante per ora e non occupa spazio nel file |
| 11/11 | Header `.h` accanto ai `.c`, inclusi dalla radice con `-I.` (`#include "drivers/vga.h"`) | Rispetta la struttura delle cartelle; ogni include dice dove sta il file |
| 11/11 | Ammessi gli header del compilatore `stdint.h` e `stdarg.h` | Non sono libreria standard: gcc li fornisce anche con `-ffreestanding` |
| 11/11 | `kputchar` scrive sempre su VGA **e** seriale; seriale a polling, 38400 8N1 | Log identici sulle due uscite, test automatici sulla seriale; niente interrupt prima della fase 2 |
| 11/11 | Compilazione con `-O0 -g` | In GDB il codice segue il sorgente riga per riga |
| 11/11 | Bersagli extra `make test` (avvio senza finestra + controllo seriale) e `make run-nox` | Verifica automatica a ogni passo, utile anche senza interfaccia grafica |
| 11/11 | Simboli `kernel_start` / `kernel_end` esportati da `linker.ld` | Serviranno all'allocatore fisico della fase 3 |

## Problemi noti
_(nessun bug aperto)_

- 11/11 — Fasi 0 e 1 sviluppate e provate in un container Linux (gcc 13.3, nasm 2.16, ld 2.42,
  QEMU 8.2.2), **non ancora sul PC dell'autore in WSL2**. `make run` (finestra grafica) lì non
  è stato provato: lo schermo VGA è stato verificato leggendo la memoria a `0xB8000` dal monitor di QEMU.
- 11/11 — ✅ risolto: `make test` si bloccava in un terminale vero (WSL2) perché usava `timeout` insieme a `-serial stdio`. Ora la seriale va su file. Dettagli in `docs/notes/fase1-boot.md`, "Errori incontrati".
- 11/11 — `kprintf` non supporta larghezza/riempimento (es. `%08x`) né `%u`: da aggiungere quando serviranno.

## Registro delle sessioni
| Data | Modello | Cosa è stato fatto |
|---|---|---|
| 11/11 | — | Scelta del progetto, preparati CLAUDE.md, PLAN.md, NOTES_TEMPLATE.md |
| 11/11 | Fable 5.2 | Fase 0 (struttura, `Makefile`, `linker.ld`, verifica toolchain) e Fase 1 (boot Multiboot, driver VGA e seriale, `kprintf`). `make test` passa. Scritta `docs/notes/fase1-boot.md` |
