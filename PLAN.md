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
| 0 | Ambiente: WSL2, gcc, nasm, QEMU, GDB, Makefile, repo | mer 11 | ⬜ da fare |
| 1 | Boot Multiboot, output VGA e seriale, `kprintf` | mer 11 | ⬜ da fare |
| 2 | GDT, IDT, eccezioni, PIC, timer, tastiera | gio 12 | ⬜ da fare |
| 3 | Memoria: allocatore fisico (bitmap), paginazione, heap `kmalloc` | ven 13 | ⬜ da fare |
| 4 | Processi: PCB, context switch, scheduler round-robin | sab 14 | ⬜ da fare |
| 5 | Syscall (`int 0x80`) e programmi in user mode | dom 15 | ⬜ da fare |
| 6 | Filesystem in RAM e shell (`ls`, `cat`, `ps`, `run`) | lun 16 | ⬜ da fare |
| 7 | Demo nel browser con v86, README, rifiniture | mar 17+ (anche con Opus) | ⬜ da fare |

Legenda: ⬜ da fare · 🟨 in corso · ✅ fatta · ⚠️ fatta con problemi noti

## Prossimo passo
Fase 0: installare la toolchain in WSL2 e creare il repository con la struttura di cartelle.

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

## Problemi noti
_(nessuno per ora)_

## Registro delle sessioni
| Data | Modello | Cosa è stato fatto |
|---|---|---|
| 11/11 | — | Scelta del progetto, preparati CLAUDE.md, PLAN.md, NOTES_TEMPLATE.md |
