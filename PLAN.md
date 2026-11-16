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
| 5 | Syscall (`int 0x80`) e programmi in user mode | dom 15 | ✅ fatta |
| 6 | Filesystem in RAM e shell (`ls`, `cat`, `ps`, `run`) | lun 16 | ✅ fatta |
| 7 | Demo nel browser con v86, README, rifiniture | mar 17+ (anche con Opus) | ✅ fatta |

Legenda: ⬜ da fare · 🟨 in corso · ✅ fatta · ⚠️ fatta con problemi noti

## Prossimo passo
Le sette fasi del piano sono completate. Restano all'autore alcune scelte e verifiche:

1. Provare sul proprio PC (WSL2) `make test`, `make run` e `make web-run`.
2. Completare i segnaposto `TODO` del `README.md`: nome e cognome, corso e anno
   accademico, descrizione del proprio ruolo nella sezione sull'uso dell'IA.
3. Scegliere una licenza (oggi non c'è nessun file `LICENSE`).
4. Decidere se pubblicare la demo (per esempio con GitHub Pages): `build/web/` è già
   una cartella autonoma; servirebbe un workflow che la costruisca e la pubblichi.

Possibili sviluppi, in ordine di interesse didattico: argomenti ai programmi
(`run conta 10`); pipe e redirezione nella shell; semafori al posto degli interrupt
disabilitati; pagine di guardia per gli stack; un vero disco (ATA) con un filesystem
persistente; un formato eseguibile ELF.

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
web/               pagina della demo nel browser (v86)
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
| 14/11 | GDT a 6 righe: aggiunti codice utente `0x1B`, dati utente `0x23`, TSS `0x28`; del TSS si usano solo `ss0`/`esp0` | Minimo richiesto dalla CPU per passare da ring 3 a ring 0; il cambio di contesto resta software |
| 14/11 | Syscall con `int 0x80`: numero in `EAX`, argomenti in `EBX`/`ECX`/`EDX`, risultato in `EAX`, `-1` = errore; numeri in `kernel/syscall_numbers.h` | Stessa convenzione di Linux a 32 bit; un solo header condiviso tra kernel e programmi |
| 14/11 | Le syscall girano a interrupt spenti (interrupt gate); `write` limitata a 4096 byte | Nessuna concorrenza dentro il kernel; il limite evita di bloccare il sistema a lungo |
| 14/11 | Ogni processo utente ha la sua page directory; le righe del kernel sono copiate e puntano alle stesse page table | Kernel mappato ovunque agli stessi indirizzi: syscall e interrupt non cambiano `CR3` |
| 14/11 | Page table dell'heap del kernel create in anticipo (`paging_reserve_kernel_tables`) | Le directory dei processi copiano le righe: una tabella creata dopo non sarebbe visibile in quelle esistenti |
| 14/11 | Layout utente: programma a `0x40000000` (64 KiB), stack sotto `0xB0000000` (16 KiB); zona utente `0x40000000`–`0xC0000000` | Dimensioni fisse: loader banale, nessun formato da interpretare |
| 14/11 | Programmi utente come binari piatti (`user/user.ld`), incorporati nel kernel con `incbin` (`proc/user_programs.asm`) — *superata in parte il 15/11: ora passano da `fs/initial_files.asm` e dal filesystem in RAM* | Non c'è ancora un disco né un filesystem; niente loader ELF da scrivere |
| 14/11 | Puntatori ricevuti dalle syscall sempre validati (`paging_is_user_range`) | Il kernel può leggere tutto: senza controllo un programma gli farebbe leggere o stampare memoria non sua |
| 14/11 | Eccezione provocata in ring 3 = terminazione del solo processo (`process_kill_current`); in ring 0 = panic | L'errore di un programma non deve fermare il sistema |
| 14/11 | `process_kill` per terminare un altro processo dall'esterno | Serve per i programmi che non terminano; è sicuro finché il kernel non ha lock |
| 15/11 | La shell è un programma utente (`user/shell.c`), avviata dal kernel come primo processo e riavviata se termina | Dimostra che le syscall bastano a costruire un'interfaccia; il processo 0 non può farlo perché è l'idle |
| 15/11 | Nuovo stato `PROCESS_BLOCKED` con `process_block` / `process_wake` | Attesa passiva di un evento (tasto, fine di un figlio) invece di polling |
| 15/11 | Tastiera: buffer circolare di 128 caratteri, `read_key` bloccante, un solo lettore; eco e Backspace nella libreria utente | Schema produttore-consumatore; il driver deposita e basta |
| 15/11 | Riferimenti tra processi tramite PID (`parent_pid`, `waiting_for_pid`, lettore della tastiera), mai tramite puntatori al PCB | Un PCB può essere liberato; un PID al massimo non si trova più |
| 15/11 | `spawn(nome)` + `wait(pid)` invece di `fork`/`exec`; un processo terminato resta zombie finché il genitore non fa `wait` | Niente copia dello spazio di indirizzi; il codice di uscita deve sopravvivere al processo |
| 15/11 | Ctrl+C gestito nel driver della tastiera: termina il programma di cui qualcuno attende la fine | Deve funzionare anche con programmi che non leggono la tastiera |
| 15/11 | Filesystem in RAM piatto: tabella fissa di 32 file, 64 KiB l'uno, contenuto in un blocco dell'heap; scrittura solo dell'intero file | Senza disco non servono blocchi né indici; la versione più semplice che supporti creare e cancellare |
| 15/11 | Syscall sui file per nome, senza descrittori (`file_read` con offset) | Evita la tabella dei file aperti per processo e la sua pulizia |
| 15/11 | Contenuto iniziale del filesystem incorporato con `fs/initial_files.asm` (una riga per file); sostituisce `proc/user_programs.asm` e la tabella in `proc/programs.c` | Aggiungere un file o un programma ora richiede una sola riga |
| 15/11 | Programmi riconosciuti dal numero magico `MOS1` nei primi 4 byte; l'esecuzione parte a `0x40000004` | Impedisce di "eseguire" un file di testo |
| 15/11 | Syscall con quattro argomenti (aggiunto `ESI`); 13 syscall in tutto | `file_read` ha bisogno di nome, buffer, quantità e posizione |
| 15/11 | Prove automatiche solo se la riga di comando Multiboot contiene `autotest` (`make test`); `make run` va dritto alla shell | Avvio normale pulito; stessa immagine del kernel per uso e collaudo |
| 15/11 | In collaudo un thread del kernel "digita" nella shell (`kernel/autotest.c`) e poi spegne QEMU (porta `0x604`) | Prova ripetibile di tutta la catena tastiera → shell → syscall; `make test` dura 2 secondi invece di 8 |
| 16/11 | Demo web: v86 avvia direttamente `build/kernel.elf` come kernel Multiboot; niente immagine ISO né GRUB | Ha funzionato al primo tentativo senza modifiche al kernel |
| 16/11 | v86 e i BIOS non stanno nel repository: `make web` li scarica in `web/vendor/` a versioni fissate (`v86` 0.5.473 da npm, BIOS dal commit `315b2b0`) e ne verifica lo SHA-256 | Repository piccolo e fatto solo di codice nostro; demo riproducibile; un download alterato viene rifiutato |
| 16/11 | `build/web/` è una cartella autonoma servita da `python3 -m http.server` (`make web-run`); nessuna pubblicazione automatica | Funziona senza rete dopo la prima preparazione; pubblicare è una scelta dell'autore |
| 16/11 | Il kernel azzera da sé la sezione `.bss` in `boot/boot.asm` (simboli `bss_start`/`bss_end` in `linker.ld`) | Dopo un riavvio a caldo la RAM non è a zero e il bootloader può non azzerarla |
| 16/11 | README secondo lo schema della skill `readme-progetto`, con segnaposto `TODO` per i dati che solo l'autore conosce | Meglio un segnaposto onesto che un dato inventato |

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
- 16/11 — La fase 7 è stata provata solo nel container: la demo web con Chromium senza interfaccia pilotato in automatico (avvio, comandi, Ctrl+C, tre riavvii, larghezza da telefono), non con un browser vero su Windows/WSL2.
- 16/11 — La demo web non ha una prova automatica nel repository (quelle usate durante lo sviluppo richiedono Playwright). Su telefono la pagina si vede ma non è utilizzabile: serve una tastiera fisica.
- 16/11 — Il diagramma Mermaid del `README.md` non è stato disegnato in locale: va controllato come appare su GitHub.
- 16/11 — `make web` dipende da due siti esterni (registro npm e GitHub) alla prima esecuzione; se le versioni fissate venissero rimosse andrebbero aggiornate insieme a `web/vendor.sha256`.
- 16/11 — Pubblicare la demo significa ridistribuire v86 (BSD) e i BIOS (LGPL): le rispettive licenze vanno accompagnate ai file (quella di v86 viene già copiata in `build/web/v86/`).
- 16/11 — In v86 la mappa della memoria Multiboot dichiara libera anche la zona `0xC0000`-`0xFFFFF` (ROM): innocuo, perché il kernel non usa mai il primo MiB.
- 16/11 — ✅ risolto in fase 7: il kernel dipendeva dall'azzeramento della `.bss` da parte del bootloader e non sopravviveva a un riavvio a caldo.
- 15/11 — Le fasi 5 e 6 sono state provate solo nel container (40 esecuzioni consecutive di `make test` senza errori, 16 e 512 MiB di RAM, tasti veri inviati dal monitor di QEMU), non su WSL2.
- 15/11 — Filesystem: nessuna persistenza (tutto si perde allo spegnimento), nessuna sottocartella, nessun permesso; un file si può solo riscrivere per intero; il puntatore restituito da `fs_find` vale solo finché il file non viene sostituito o cancellato.
- 15/11 — Shell: nessun argomento passato ai programmi, niente pipe, redirezione, esecuzione in background, cronologia o tasti freccia. Layout di tastiera solo americano.
- 15/11 — `wait` restituisce -1 sia per un errore sia per un programma terminato dal kernel o da Ctrl+C: la shell non distingue i casi.
- 15/11 — Un processo terminato resta visibile in `ps` come "terminato" finché il processo 0 non trova un momento libero per fare pulizia. Con un programma che non cede mai la CPU (`run ciclo`) pulizia e riavvio della shell restano fermi fino a Ctrl+C.
- 15/11 — Tastiera: un solo processo alla volta può essere in attesa di un tasto; se due processi leggono insieme, il primo resta bloccato.
- 15/11 — Lo spegnimento a fine collaudo funziona solo in QEMU. Su un altro emulatore `make test` terminerebbe per timeout (30 secondi).
- 15/11 — ✅ risolto in fase 6: per aggiungere un programma ora basta una riga in `fs/initial_files.asm`; e il processo 0 non stampa più le righe "Timer: N s".
- 14/11 — Anche la fase 5 è stata provata solo nel container (60 avvii consecutivi senza errori, più 16 e 512 MiB di RAM), non su WSL2.
- 14/11 — Il processo 0 (idle) riceve la CPU solo se nessun altro è pronto: un processo che gira all'infinito gli impedisce di lavorare, compresa la pulizia dei processi terminati. Qualsiasi compito attivo (shell, supervisione) va affidato a un processo normale.
- 14/11 — Programmi utente: tutte le pagine sono scrivibili (anche il codice); dimensioni fisse (64 KiB + 16 KiB di stack, senza pagina di guardia); nessun argomento a `main`; niente `fork`/`exec`/`wait`; per aggiungerne uno vanno toccati tre file.
- 14/11 — Le syscall non sono interrompibili: una syscall lenta ritarderebbe timer e tastiera. Oggi nessuna lo è.
- 14/11 — `process_kill` presuppone un kernel senza lock: se in futuro si aggiungono semafori o lock, uccidere un processo che ne tiene uno lo lascerebbe bloccato per sempre.
- 14/11 — I controlli "nessuna pagina persa" nei test presuppongono che l'heap non cresca durante la prova (le pagine dell'heap non tornano mai al PMM).
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
| 14/11 | Fable 5.2 | Fase 5 completa in tre passi: segmenti utente, TSS e syscall `int 0x80`; una page directory per processo; programmi utente (`user/`) compilati a parte, caricati dal kernel ed eseguiti in ring 3, con protezione verificata. Trovata la starvation del processo 0 nei test. `make test` passa. Scritta `docs/notes/fase5-modo-utente.md` |
| 15/11 | Fable 5.2 | Fase 6 completa in tre passi: tastiera con buffer e lettura bloccante; filesystem in RAM con i programmi come file; shell in modalità utente con 8 nuove syscall, Ctrl+C e collaudo automatico. `make test` passa. Scritta `docs/notes/fase6-fs-shell.md` |
| 16/11 | Fable 5.2 | Fase 7: demo nel browser con v86 (`make web`, `make web-run`), provata con un browser pilotato in automatico; trovato e corretto il mancato azzeramento della `.bss`; scritto il `README.md`. `make test` passa. Scritta `docs/notes/fase7-demo-web.md` |
