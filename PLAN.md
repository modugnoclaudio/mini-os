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
| 2 | GDT, IDT, eccezioni, PIC, timer, tastiera | gio 12 | ✅ fatta |
| 3 | Memoria: allocatore fisico (bitmap), paginazione, heap `kmalloc` | ven 13 | ✅ fatta |
| 4 | Processi: PCB, context switch, scheduler round-robin | sab 14 | ✅ fatta |
| 5 | Syscall (`int 0x80`) e programmi in user mode | dom 15 | ⬜ da fare |
| 6 | Filesystem in RAM e shell (`ls`, `cat`, `ps`, `run`) | lun 16 | ⬜ da fare |
| 7 | Demo nel browser con v86, README, rifiniture | mar 17+ (anche con Opus) | ⬜ da fare |

Legenda: ⬜ da fare · 🟨 in corso · ✅ fatta · ⚠️ fatta con problemi noti

## Prossimo passo
Fase 5: syscall (`int 0x80`) e programmi in user mode.
Primo passo concreto: aggiungere alla GDT i segmenti utente (ring 3) e il TSS, che dice
alla CPU quale stack del kernel usare quando arriva un interrupt mentre gira codice utente;
poi saltare in modalità utente con `iret` ed eseguire una prima syscall di stampa.
Solo dopo: uno spazio di indirizzi (page directory) per ogni processo.

Da ricordare per la fase 5:
- ogni processo avrà la sua page directory, e le mappature del kernel (identità e heap)
  dovranno comparire uguali in tutte; lo scheduler dovrà cambiare `CR3` e lo stack nel TSS;
- `struct registers` ha già i campi `user_esp` / `user_ss`, e la IDT va estesa con il
  gate `0x80` accessibile da ring 3.

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
| 11/11 | `make test` scrive la seriale su file (`-serial file:`) invece di `-serial stdio` | Con `timeout` QEMU veniva sospeso quando toccava il terminale |
| 11/11 | GDT "piatta" con 3 righe: nulla, codice kernel `0x08`, dati kernel `0x10` | Protezione affidata alla paginazione; segmenti utente e TSS si aggiungono nella fase 5 |
| 11/11 | Interrupt: 48 stub in assembly → un solo `interrupt_dispatch` in C → tabella di gestori (`interrupt_register_handler`) | I driver si registrano da soli; una sola forma dello stack (`struct registers`) per tutti |
| 11/11 | Tutte le righe della IDT sono "interrupt gate" (niente interrupt annidati) | Più semplice da ragionare; i gestori devono solo essere brevi |
| 11/11 | IRQ rimappati su 32-47; EOI inviato **prima** di chiamare il gestore | 8-15 sono eccezioni della CPU; l'EOI anticipato evita problemi quando il timer farà il context switch (fase 4) |
| 11/11 | Timer PIT a 100 Hz (`TIMER_HZ`) | Un tick = 10 ms, facile da convertire; base per il quanto di tempo dello scheduler |
| 11/11 | Tastiera con layout US, eco diretto dal gestore, tasti estesi ignorati | Minimo indispensabile; buffer dei tasti rimandato alla shell (fase 6) |
| 11/11 | Eccezione senza gestore = kernel panic con stampa dei registri | Non esistono ancora processi da terminare singolarmente |
| 14/11 | PMM: bitmap fissa di 32 KiB nella `.bss`, RAM gestita fino a 1 GiB (`PMM_MAX_MEMORY`), primo MiB mai distribuito | Niente da piazzare a mano in memoria; il primo MiB contiene BIOS, video e dati Multiboot |
| 14/11 | Voci della mappa Multiboot lette come coppie di `uint32_t` (metà bassa e alta) | Evita l'aritmetica a 64 bit, che a 32 bit richiederebbe funzioni di supporto di libgcc |
| 14/11 | Tutta la RAM mappata in identità (virtuale = fisico); niente kernel "higher half" | Il kernel scrive in qualsiasi pagina fisica, page table comprese, usandone l'indirizzo |
| 14/11 | Pagina 0 non mappata | Un puntatore nullo diventa un page fault invece di una lettura silenziosa |
| 14/11 | Layout virtuale: `0`–`1 GiB` kernel in identità; `0xD0000000`–`0xD1000000` heap del kernel; in mezzo spazio per i programmi utente | Zone separate e fisse, facili da riconoscere in un indirizzo |
| 14/11 | Heap: lista di blocchi con header di 16 byte, first fit, divisione e fusione; cresce a richiesta, non si restringe | La versione più semplice che mostri davvero frammentazione e coalescenza |
| 14/11 | Page fault = sempre kernel panic (con indirizzo da `CR2` e causa) | Nessun demand paging né swap in programma |
| 14/11 | Prove all'avvio in `mm/memtest.c`; `make test` fallisce se compare `ERRORE` | Ogni livello della memoria è verificato a ogni avvio |
| 14/11 | `memset` / `memcpy` in `lib/string.c` con i nomi standard | gcc può inserire chiamate a queste funzioni di sua iniziativa |
| 14/11 | Fase 4 con thread del kernel (memoria condivisa); processi utente rimandati alla fase 5 | Stesso meccanismo di context switch, senza cambi di privilegio né di spazio di indirizzi |
| 14/11 | PCB (`struct process`) e stack da 8 KiB allocati con `kmalloc`, in lista circolare | Nessun limite fisso al numero di processi; forma naturale del round-robin |
| 14/11 | Nel PCB si salva solo `ESP`; `context_switch` salva sullo stack EBP, EBX, ESI, EDI | Sono i soli registri che la convenzione cdecl impone di conservare |
| 14/11 | Round-robin con quanto di 5 tick (50 ms), `SCHEDULER_QUANTUM` | Abbastanza breve da vedere l'alternanza, abbastanza lungo da non passare il tempo a commutare |
| 14/11 | Il processo 0 (`kernel_main`) è l'idle: scelto solo se nessuno è pronto, interrotto a ogni tick; libera i processi terminati | Serve sempre un processo eseguibile; un processo non può liberare il proprio stack |
| 14/11 | Mutua esclusione spegnendo gli interrupt (`interrupts_save_and_disable` / `interrupts_restore`) | Su una sola CPU basta, e si annida correttamente; niente lock o semafori per ora |
| 14/11 | `kprintf`, `kmalloc`/`kfree`, `pmm_alloc_page`/`pmm_free_page` sono sezioni critiche | Usate da più processi: senza protezione righe mescolate e strutture rovinate |
| 14/11 | `process_sleep` realizzata controllando tutta la lista a ogni tick | Poche righe; una coda ordinata servirebbe solo con molti processi |

## Problemi noti
_(nessun bug aperto)_

- 11/11 — Fasi 0 e 1 sviluppate e provate in un container Linux (gcc 13.3, nasm 2.16, ld 2.42,
  QEMU 8.2.2), **non ancora sul PC dell'autore in WSL2**. `make run` (finestra grafica) lì non
  è stato provato: lo schermo VGA è stato verificato leggendo la memoria a `0xB8000` dal monitor di QEMU.
- 11/11 — ✅ risolto: `make test` si bloccava in un terminale vero (WSL2) perché usava `timeout` insieme a `-serial stdio`. Ora la seriale va su file. Dettagli in `docs/notes/fase1-boot.md`, "Errori incontrati".
- 11/11 — Anche la fase 2 è stata provata solo nel container. La tastiera è stata verificata inviando i tasti dal monitor di QEMU (`sendkey`), non con una tastiera vera nella finestra.
- 14/11 — ✅ risolto in fase 4 (`kprintf` è ora una sezione critica). Voce originale: 11/11 — `kprintf` non è protetta dagli interrupt: l'eco di un tasto può finire in mezzo a una riga stampata dal ciclo principale. Innocuo per ora; da sistemare quando ci saranno più processi (fase 4).
- 11/11 — Tastiera: layout solo US (simboli e accentate non corrispondono a una tastiera italiana); frecce e altri tasti estesi ignorati; niente Caps Lock né Ctrl/Alt.
- 11/11 — PIC: gli interrupt "spuri" (IRQ 7 e 15) non sono trattati in modo speciale. In QEMU non si presentano.
- 14/11 — Anche la fase 4 è stata provata solo nel container (60 avvii consecutivi senza errori, più 16 e 512 MiB di RAM), non su WSL2.
- 14/11 — Stack dei processi: 8 KiB senza pagina di guardia. Uno stack overflow sovrascrive in silenzio il blocco di heap vicino.
- 14/11 — Le sezioni critiche spengono gli interrupt per tutta la durata di una `kprintf`: su hardware reale con seriale lenta si potrebbero perdere tick del timer. In QEMU non succede.
- 14/11 — Scheduler: nessuna priorità; `process_sleep` e `pick_next` scorrono tutta la lista (costo lineare nel numero di processi); il contatore dei tick va in overflow dopo circa 497 giorni.
- 14/11 — Non esiste ancora un modo per attendere la fine di un processo (`wait`/`join`) né primitive di sincronizzazione (semafori): i test usano una variabile condivisa e `hlt`.
- 14/11 — La fase 3 è stata provata solo nel container (QEMU con 16, 128, 512 e 2048 MiB di RAM), non ancora su WSL2. Le fasi 0-2 invece sono state confermate dall'autore sul suo PC.
- 14/11 — L'output di avvio supera le 25 righe: sullo schermo VGA le prime scorrono via. Si legge per intero sulla seriale.
- 14/11 — PMM: `pmm_alloc_page` fa una ricerca lineare dall'inizio a ogni chiamata (lenta con memoria quasi piena); alloca solo una pagina alla volta, non zone contigue.
- 14/11 — Heap: non restituisce mai le pagine al PMM; il riconoscimento del double free si basa sull'header e può sfuggire se il blocco è stato riassegnato; nessuna protezione contro scritture oltre la fine di un blocco.
- 14/11 — ✅ risolto in fase 4 per PMM e heap (sezioni critiche); `paging_map_page` da sola resta non protetta. Voce originale: PMM, paginazione e heap non sono protetti dagli interrupt: oggi nessun gestore di interrupt li usa, ma dalla fase 4 serviranno sezioni critiche.
- 14/11 — La struttura Multiboot viene letta solo in `pmm_init`; se in futuro servirà dopo (es. moduli), con un bootloader diverso da QEMU potrebbe trovarsi fuori dal primo MiB e non essere protetta.
- 11/11 — `kprintf` non supporta larghezza/riempimento (es. `%08x`) né `%u`: da aggiungere quando serviranno.

## Registro delle sessioni
| Data | Modello | Cosa è stato fatto |
|---|---|---|
| 11/11 | — | Scelta del progetto, preparati CLAUDE.md, PLAN.md, NOTES_TEMPLATE.md |
| 11/11 | Fable 5.2 | Fase 0 (struttura, `Makefile`, `linker.ld`, verifica toolchain) e Fase 1 (boot Multiboot, driver VGA e seriale, `kprintf`). `make test` passa. Scritta `docs/notes/fase1-boot.md` |
| 11/11 | Fable 5.2 | Corretto `make test` che si bloccava su WSL2. Fase 2 completa in quattro passi: GDT, IDT ed eccezioni, PIC e timer, tastiera. `make test` passa. Scritta `docs/notes/fase2-interrupt.md` |
| 14/11 | Fable 5.2 | Fase 3 completa in tre passi: mappa Multiboot e allocatore fisico a bitmap, paginazione con RAM in identità e gestore dei page fault, heap con `kmalloc`/`kfree`. Prove automatiche in `mm/memtest.c`. `make test` passa. Scritta `docs/notes/fase3-memoria.md` |
| 14/11 | Fable 5.2 | Fase 4 completa in tre passi: sezioni critiche, PCB e context switch con processi cooperativi, scheduler round-robin con prelazione e `sleep`. Trovata e corretta una race condition nei test. `make test` passa. Scritta `docs/notes/fase4-processi.md` |
