# Fase 7 — Demo nel browser con v86, README, rifiniture

## In una frase
Lo stesso kernel che gira in QEMU ora si avvia dentro una pagina web, e il
repository ha un README che spiega cos'è il progetto e come provarlo.

## Il problema
Un sistema operativo è difficile da mostrare: per vederlo bisogna installare un
compilatore e un emulatore. Chi apre il repository (un docente, un compagno, chi
valuta un curriculum) di solito non lo farà.

Servono quindi due cose:
1. una **demo che parte con un clic**, senza installare nulla;
2. un **README** che in pochi secondi dica cosa fa il progetto e in pochi minuti
   come avviarlo.

Preparando la demo è emerso un terzo problema, reale: il kernel **non sopravviveva
a un riavvio** (vedi "Errori incontrati").

## Collegamento con la teoria
- **Emulazione e macchine virtuali**: v86 simula in software CPU, memoria e
  dispositivi di un PC. Il nostro kernel non sa di non essere su hardware vero.
- **Portabilità tra piattaforme**: lo stesso file `build/kernel.elf` gira in due
  emulatori diversi perché entrambi rispettano le stesse interfacce (lo standard
  Multiboot, i dispositivi del PC).
- **Bootstrap e immagine di un programma in memoria**: chi è responsabile di
  azzerare la sezione `.bss`? È il punto che il riavvio ha messo in luce.
- **Avvio a freddo e avvio a caldo**: all'accensione la RAM (emulata) è a zero; dopo
  un riavvio contiene ancora i dati della sessione precedente.

## Come funziona
**La demo.** La pagina `web/index.html` carica v86, un emulatore di PC scritto in
JavaScript e WebAssembly, e gli passa tre cose:
```
v86  ←  seabios.bin, vgabios.bin    il BIOS del PC simulato (lo stesso tipo che usa QEMU)
     ←  kernel.elf                   il nostro kernel, come opzione "multiboot"
     ←  un <div> della pagina        lo "schermo" su cui disegnare le 25 righe di testo
```
Come QEMU con `-kernel`, v86 sa fare da bootloader Multiboot: legge l'header, carica
il kernel a 1 MiB e salta a `_start`. **Non è servita nessuna immagine disco né
alcuna modifica al kernel per il primo avvio.**

Da lì in poi tutto è identico: il kernel programma PIC e timer emulati, riceve gli
scancode della tastiera (v86 traduce i tasti premuti nella pagina in scancode PS/2)
e scrive nella memoria video, che v86 mostra nel `<div>`.

La pagina ascolta anche la **porta seriale** emulata: quando vi compare `mini-os> `
sa che l'avvio è finito e cambia l'indicatore di stato. È lo stesso canale che usa
`make test` in QEMU.

**La preparazione.** v86 non fa parte del repository. `make web`:
1. scarica, solo la prima volta, i file di v86 in `web/vendor/` (ignorata da git),
   a versioni fissate, e ne verifica l'impronta SHA-256 con `web/vendor.sha256`;
2. copia in `build/web/` la pagina, il kernel e i file di v86.

`build/web/` è una cartella autonoma. `make web-run` la serve con il server web
incluso in Python, perché un browser non lascia scaricare file a una pagina aperta
con un doppio clic.

**L'azzeramento della `.bss`.** `boot/boot.asm`, appena impostato lo stack, azzera
tutta la sezione `.bss` con `rep stosb`, usando i simboli `bss_start` e `bss_end`
definiti in `linker.ld`. Solo dopo chiama `kernel_main`.

**Il README** segue lo schema dei progetti open source curati: slogan, immagine,
schema dell'architettura, avvio in tre comandi, tabelle per ciò che è
confrontabile (componenti, comandi, syscall), limiti dichiarati, crediti.

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|
| `web/index.html` | Pagina della demo: configura v86, stato, pulsante di riavvio | — |
| `web/vendor.sha256` | Impronte attese dei file di v86 | — |
| `Makefile` | (aggiunta) preparazione e avvio della demo | bersagli `web`, `web-run` |
| `boot/boot.asm` | (modifica) azzeramento della `.bss` | `_start` |
| `linker.ld` | (modifica) simboli che delimitano la `.bss` | `bss_start`, `bss_end` |
| `README.md` | Presentazione del progetto | — |
| `docs/images/demo.png` | Immagine della shell nel browser | — |
| `.gitignore` | (modifica) esclude `web/vendor/` | — |

## Concetti di C incontrati
**Chi mette a zero le variabili globali?**
```c
static struct file files[FS_MAX_FILES];   // "parte tutta a zero"... ma chi lo fa?
static uint32_t next_pid = 0;
```
Il C *promette* che le globali non inizializzate valgono 0. Non è magia: quelle
variabili stanno nella sezione `.bss`, che nel file eseguibile non occupa spazio, e
qualcuno deve riempirla di zeri prima che il programma parta. In un programma
normale lo fa il sistema operativo quando lo carica. Nel kernel non c'è un sistema
operativo sotto: se non lo fa il bootloader, tocca a noi.
In Java il problema non si vede mai: la JVM azzera ogni campo e ogni array alla creazione.

**Globali inizializzate e non inizializzate stanno in posti diversi.**
`static int x = 5;` finisce in `.data` (il valore 5 è scritto nel file);
`static int y;` e `static int z = 0;` finiscono in `.bss` (nel file c'è solo la dimensione).
Quasi tutte le globali del kernel sono del secondo tipo: per questo, con la `.bss`
non azzerata, quasi tutto lo stato sopravviveva al riavvio.

**Simboli del linker come confini.** `bss_start` e `bss_end` non sono variabili:
sono indirizzi definiti in `linker.ld`, come `kernel_start` e `kernel_end` della
fase 1. Dall'assembly si usano direttamente come numeri.

**`rep stosb`.** Un'istruzione x86 che ripete una scrittura: è un `memset` in una
riga. Qui serve l'assembly perché l'azzeramento va fatto prima di poter chiamare
qualsiasi funzione C (lo stack stesso sta nella `.bss`).

## Decisioni prese
- **Avvio diretto del kernel Multiboot in v86**, senza immagine ISO. Scartato il
  piano di riserva con GRUB (`grub-mkrescue`): non è servito.
- **v86 scaricato da `make web`, non incluso nel repository.** Sono circa 2,5 MiB di
  file binari di terzi: fuori da git il repository resta piccolo e fatto solo di
  codice nostro. Costo: la prima preparazione richiede la rete.
- **Versioni fissate e impronte verificate** (`V86_VERSION`, `V86_COMMIT`,
  `web/vendor.sha256`): la demo non cambia comportamento da un giorno all'altro, e
  un file scaricato diverso dall'atteso blocca la preparazione.
- **Pagina autonoma, senza dipendenze esterne a tempo di esecuzione**: una volta
  preparata, `build/web/` funziona anche senza rete e su qualsiasi server.
- **Nessuna pubblicazione automatica** (per esempio GitHub Pages): è una scelta che
  spetta all'autore e richiede di abilitarla nelle impostazioni del repository.
- **Il kernel azzera la propria `.bss`** invece di fidarsi del bootloader. È ciò
  che fanno i kernel veri.
- **README con segnaposto `TODO`** per i dati che solo l'autore conosce (nome,
  corso, licenza), invece di inventarli. Compilati il 20/11 con le risposte
  dell'autore.
- **Licenza MIT**, scelta dall'autore il 20/11 (file `LICENSE`). Riguarda solo il
  codice di mini-os: v86 e i BIOS restano sotto le loro licenze.

## Come provarlo
```bash
make web-run
```
La prima volta scarica v86 (serve la rete) e stampa quattro righe `OK`, una per ogni
file verificato. Poi apri nel browser:
```
http://localhost:8000
```
In pochi secondi l'indicatore passa da "Avvio del PC simulato…" a
"In esecuzione" e compare il prompt. Da provare:
```
ls
run hello
scrivi nota.txt ciao
cat nota.txt
run ciclo          poi Ctrl+C
```
Premi **Riavvia**: il sistema riparte, la shell ha di nuovo `pid 1` e `nota.txt`
non c'è più. Per fermare il server: `Ctrl+C` nel terminale.

Su WSL2 il server gira dentro Linux, ma di norma `localhost:8000` si apre dal browser di Windows.

`make test` resta la prova del kernel in QEMU e deve continuare a terminare con `>>> TEST OK`.

## Errori incontrati
**Sintomo:** nella demo, dopo aver premuto "Riavvia", comparivano sette righe
`HEAP: ERRORE, kfree(...): non e' un blocco valido`, la shell ripartiva con `pid 3`
invece di `pid 1`, e un file creato prima del riavvio era ancora nell'elenco.
**Causa:** il kernel dava per scontato che la sezione `.bss` fosse a zero all'avvio.
Al primo avvio lo era, perché la RAM emulata nasce azzerata. Al riavvio la RAM non
viene ripulita: la tabella dei file, il contatore dei PID e le
altre globali conservavano i valori della sessione precedente. `fs_init` trovava
quindi dei file "già esistenti" e tentava di liberarne i vecchi contenuti, che
puntavano in un heap che non esisteva più.
**Soluzione:** azzerare la `.bss` in `boot/boot.asm`, prima di chiamare `kernel_main`.
**Come è stato trovato:** provando la pagina con un browser pilotato in automatico,
compreso il pulsante di riavvio. In QEMU non si era mai visto, perché ogni prova
partiva da una macchina appena accesa.
**Lezione:** "ha sempre funzionato" può voler dire soltanto "non l'ho mai provato in
quella situazione". Un ambiente diverso (qui un secondo emulatore) è un ottimo test:
fa cadere le ipotesi che non sapevamo di aver fatto.

**Sintomo:** nessuno, ma a un primo controllo dopo la correzione restava un dubbio:
e le globali inizializzate a un valore diverso da zero (sezione `.data`)?
**Verifica:** nel kernel sono solo due. `vga_buffer` non viene mai modificata;
`heap_end` viene reimpostata da `kheap_init`. Tre riavvii consecutivi nella demo
ripartono tutti con `pid 1`, 8 file e nessun errore.

**Trappole da riconoscere:**

| Sintomo | Causa tipica |
|---|---|
| La pagina resta su "Non avviato" con un avviso | Aperta con un doppio clic (`file://`) invece che con `make web-run` |
| `sha256sum` segnala un file `FAILED` durante `make web` | Download corrotto, oppure versione di v86 cambiata senza aggiornare `web/vendor.sha256` |
| I tasti non arrivano al sistema | Il fuoco della tastiera è su un altro elemento della pagina o del browser |
| Dopo un riavvio il sistema parte "sporco" | Qualche variabile globale non viene reinizializzata: vedi sopra |

## Domande per verificare di aver capito
1. Perché lo stesso `kernel.elf` funziona sia in QEMU sia in v86 senza essere ricompilato?
2. Che differenza c'è tra le sezioni `.data` e `.bss`? Perché solo la seconda dava
   problemi dopo un riavvio?
3. Perché l'azzeramento della `.bss` è scritto in assembly e sta prima di qualsiasi
   `push`, invece di essere una chiamata a `memset` all'inizio di `kernel_main`?
4. Perché la demo non funziona aprendo `index.html` con un doppio clic, e a cosa
   serve `python3 -m http.server`?
5. A cosa servono le impronte SHA-256 in `web/vendor.sha256`?
