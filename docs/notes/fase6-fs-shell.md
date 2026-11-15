# Fase 6 — Filesystem in RAM e shell

## In una frase
All'avvio compare un prompt: si possono elencare e leggere file, crearne e
cancellarne, lanciare programmi e fermarli con Ctrl+C, tutto da una shell che è
essa stessa un programma utente.

## Il problema
Il kernel sapeva fare molte cose, ma solo se scritte nel suo codice: quali programmi
avviare, in che ordine. Mancavano i due pezzi che rendono un sistema *utilizzabile*:

1. **dare un nome ai dati**: programmi e testi devono essere *file* che si cercano
   per nome, non simboli incorporati nel kernel (filesystem);
2. **dare la parola all'utente**: qualcuno deve leggere i comandi e trasformarli in
   richieste al kernel (shell).

Entrambi richiedono cose nuove sotto: leggere la tastiera senza sprecare CPU
(processi che si *bloccano*), far nascere un processo da un altro e attenderne la
fine, e poter fermare un programma che non si ferma da solo.

## Collegamento con la teoria
- **File, directory, metadati**: `struct file` e la tabella `files` in `fs/ramfs.c`.
  Directory a un solo livello; allocazione **contigua** (un blocco per file).
- **Produttore-consumatore con buffer limitato**: il gestore della tastiera produce
  caratteri, un processo li consuma; in mezzo un **buffer circolare** (`drivers/keyboard.c`).
- **Stato "bloccato" e I/O bloccante**: `process_block` / `process_wake`. Il processo
  aspetta un *evento*, non un istante.
- **Risveglio perduto (lost wakeup)**: perché "buffer vuoto?" e "mi blocco" devono
  essere atomici, in `keyboard_read_char`.
- **Creazione di processi**: `spawn` fonde `fork` + `exec` di Unix. **`wait`** e i
  **processi zombie**: `sys_wait` e `can_be_reaped`.
- **Segnali**: Ctrl+C è il nostro `SIGINT`, `process_interrupt_foreground`.
- **Interprete dei comandi**: `user/shell.c`. Non è nel kernel: è un processo qualsiasi.
- **Formato degli eseguibili e numero magico**: `PROGRAM_MAGIC` ("MOS1").
- **init**: il primo processo utente, che il kernel riavvia se muore (`kernel_main`).
- **Parametri di avvio del kernel**: la riga di comando Multiboot (`autotest_requested`).

## Come funziona
**Il filesystem.** Una tabella di 32 caselle; ogni casella occupata ha nome,
dimensione e un puntatore al contenuto, che è un blocco dell'heap del kernel.
Scrivere un file significa allocare un blocco nuovo, copiarvi i dati e liberare
quello vecchio: un file non si modifica "sul posto". Il contenuto iniziale (testi di
`fs/root/` e programmi compilati) è incorporato nel kernel da `fs/initial_files.asm`
e copiato nel filesystem da `fs_init`.

**Dal tasto alla shell:**
```
tasto ─IRQ 1─► keyboard_handler ─► buffer circolare ─► keyboard_read_char ─► sys_read_key ─► shell
               (traduce lo scancode,   (128 caratteri)   se è vuoto: process_block()
                risveglia chi aspetta)                   il gestore farà process_wake()
```
Mentre la shell è bloccata non consuma CPU: gira il processo idle. L'eco del
carattere a schermo lo fa la shell (`read_line` in `user/lib/ulib.c`), non il driver.

**Un comando, dall'inizio alla fine** (`run hello`):
```
shell      read_line() → "run hello" → divide in comando e argomento
shell      spawn("hello")                         ── int 0x80 ──►
kernel       sys_spawn: copia il nome, program_run: fs_find("hello"),
             controlla il numero magico, crea il processo, parent_pid = shell
shell      wait(pid)                              ── int 0x80 ──►
kernel       sys_wait: process_block()   (la shell è bloccata, hello è "in primo piano")
hello      gira, stampa, exit(0)                  ── int 0x80 ──►
kernel       process_exit: stato TERMINATO, wake_parent() risveglia la shell
kernel       sys_wait riprende: legge exit_code, azzera parent_pid, ritorna
shell      stampa "[programma terminato con codice 0]" e torna al prompt
idle       process_reap_terminated libera la memoria di hello
```

**Zombie.** Tra `exit` del figlio e `wait` del genitore il PCB non può sparire:
contiene il codice di uscita. `can_be_reaped` permette la pulizia solo quando
nessuno aspetta più: nessun genitore, `wait` già fatta, o genitore morto.

**Ctrl+C.** Il driver della tastiera riconosce la combinazione e chiama
`process_interrupt_foreground`: cerca chi è bloccato in `wait` e termina il figlio
che sta aspettando. Non passa dal buffer: il programma da fermare potrebbe non
leggere mai la tastiera.

**Le syscall** sono ora 13 (`kernel/syscall_numbers.h`): alle cinque della fase 5 si
aggiungono `read_key`, `spawn`, `wait`, `ps`, `file_info`, `file_read`, `file_write`,
`file_delete`. Ogni argomento che arriva da un programma viene controllato:
`copy_string_from_user` per i nomi, `user_range_ok` per i buffer.

**Due modi di avvio.** `make run` porta direttamente alla shell. `make test` avvia
il kernel con `-append autotest`: `kernel_main` legge la riga di comando, esegue
tutte le prove delle fasi precedenti, poi `kernel/autotest.c` "digita" comandi
nella shell con `keyboard_inject_char`, verifica i risultati e spegne la macchina.

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|
| `fs/ramfs.c`, `ramfs.h` | Filesystem in RAM | `fs_init`, `fs_write`, `fs_find`, `fs_read`, `fs_delete`, `fs_get_by_index` |
| `fs/initial_files.asm` | File iniziali incorporati nel kernel | macro `FILE_INIZIALE`, `initial_files_table` |
| `fs/root/*.txt` | Testi iniziali | — |
| `fs/fstest.c` | Prove del filesystem | `fstest_run` |
| `drivers/keyboard.c`, `.h` | (riscritto) buffer, lettura bloccante, Ctrl+C | `buffer_put`, `keyboard_read_char`, `keyboard_inject_char` |
| `proc/scheduler.c` | (aggiunta) blocco e risveglio | `process_block`, `process_wake` |
| `proc/process.c`, `.h` | (aggiunta) parentela, zombie, Ctrl+C, magic | `process_find`, `can_be_reaped`, `wake_parent`, `process_interrupt_foreground` |
| `proc/programs.c` | (riscritto) programma da file | `program_run` |
| `kernel/syscall.c` | (aggiunta) 8 syscall e controlli | `sys_spawn`, `sys_wait`, `sys_file_read`, `copy_string_from_user`, `user_range_ok` |
| `kernel/autotest.c` | Collaudo automatico della shell | `autotest_thread`, `type_command`, `power_off` |
| `kernel/kernel.c` | (riscritto) avvio, modalità di collaudo, idle | `kernel_main`, `autotest_requested`, `start_shell` |
| `user/shell.c` | La shell | `main`, `split_first_word`, `command_run`, `command_cat` |
| `user/lib/ulib.c`, `crt0.asm` | (aggiunta) nuove syscall, lettura riga, magic | `read_line`, `spawn`, `wait`, `file_read` |
| `lib/string.c` | (aggiunta) | `string_length`, `string_contains` |
| `Makefile` | (modifica) `make test` in modalità di collaudo | — |

## Concetti di C incontrati
**Buffer circolare.** Un array e due indici che, arrivati in fondo, ripartono da zero:
```c
write_index = (write_index + 1) % BUFFER_SIZE;
```
In Java useresti `ArrayDeque` o `BlockingQueue`; qui è la stessa idea, a mano.

**Tagliare una stringa sul posto.** Non c'è `split`: per separare `cat file` si
scrive `'\0'` al posto dello spazio e si restituisce un puntatore al resto.
Nessuna copia, nessuna allocazione (`split_first_word` in `user/shell.c`).

**Niente `switch` sulle stringhe**, e `==` confronta indirizzi: serve una catena di
`if (string_equals(...))`.

**Stringa letterale contro array di caratteri.**
```c
char name[] = "pieno-AA";   // array modificabile, sullo stack
const char *s = "pieno-AA"; // puntatore a una costante: non si può modificare
```

**Dichiarazione anticipata.** `struct process;` dice solo "questo tipo esiste".
Basta per usare *puntatori* a quel tipo e spezza le inclusioni circolari tra header.

**PID invece di puntatori.** Tra strutture che vivono e muoiono indipendentemente
conviene ricordare un identificatore, non un indirizzo: un puntatore a memoria
liberata (*dangling pointer*) è un bug silenzioso, un PID non trovato è un caso gestibile.
In Java il garbage collector rende impossibile il problema.

**Lettura a pezzi con buffer fisso.** `cat` legge 128 byte alla volta tenendo un
`offset`: lo schema `while ((n = in.read(buf)) > 0)` di Java.

**Stesso codice in due programmi.** `lib/string.c` è collegato sia nel kernel sia
in ogni programma utente: due copie distinte, non una libreria condivisa.

**Macro di NASM con etichette locali** (`%%nome`) per generare una tabella di dati
che il C legge come array di `struct`.

## Decisioni prese
- **La shell è un programma utente**, non un thread del kernel. Costa otto syscall
  in più, ma dimostra che il kernel offre abbastanza servizi per costruirci sopra.
- **Filesystem piatto con tabella fissa** (32 file, 64 KiB ciascuno) e allocazione
  contigua sull'heap. Scartati blocchi e indici: senza disco non servono.
- **Scrittura solo "tutto il file"** (`file_write` crea o sostituisce). Niente
  aggiunta in coda né scrittura parziale.
- **Syscall sui file senza descrittori**: si passa il nome ogni volta, con un
  `offset` per le letture. Scartato il modello `open`/`read`/`close` di Unix: avrebbe
  richiesto una tabella dei file aperti per processo e la sua pulizia.
- **`spawn` al posto di `fork` + `exec`**: nessuna copia dello spazio di indirizzi.
- **Programmi riconosciuti da un numero magico** di 4 byte, `MOS1`, in testa al file.
- **Lettura di un tasto alla volta** (`read_key`); eco e Backspace gestiti nella
  libreria utente. Il driver non stampa più nulla.
- **Un solo lettore della tastiera alla volta**: basta alla shell.
- **`ps` stampa direttamente dal kernel**, invece di restituire dati al programma.
- **Ctrl+C gestito nel driver**, termina il programma in primo piano.
- **Il kernel riavvia la shell** se non esiste più.
- **Prove automatiche solo su richiesta** (`autotest` sulla riga di comando), così
  l'avvio normale è pulito; in collaudo il kernel spegne QEMU da solo.

## Come provarlo
```bash
make test
```
Dura un paio di secondi e stampa circa 250 righe; in fondo si vede la shell
pilotata dal collaudo. L'ultima riga deve essere:
```
>>> TEST OK: kernel, memoria, filesystem, processi, programmi utente e shell funzionano
```

Per usarla davvero:
```bash
make run
```
Nella finestra di QEMU, dopo una ventina di righe di avvio, compare:
```
=== mini-os: shell (pid 1) === scrivi 'help' per i comandi
mini-os>
```
Da provare, in ordine:
```
help
ls
cat leggimi.txt
run hello
scrivi appunti.txt questa e' una prova
cat appunti.txt
rm appunti.txt
run invasore          il kernel lo termina, la shell prosegue
run ciclo             non finisce mai: fermalo con Ctrl+C
ps
exit                  il kernel avvia una nuova shell
```
La tastiera è in layout americano: lettere e numeri coincidono, `-` e `.` sono dove
te li aspetti, l'apostrofo no.

## Errori incontrati
**Sintomo:** `warning: 'struct process' declared inside parameter list` seguito da
`error: conflicting types for 'process_wake'`.
**Causa:** `proc/scheduler.h` dichiarava una funzione con un parametro
`struct process *` senza che quel tipo fosse mai stato nominato prima. Il
compilatore lo considerava un tipo nuovo, locale a quella sola riga, quindi diverso
da quello usato nella definizione. Risolto con la dichiarazione anticipata `struct process;`.

**Osservazione:** durante il collaudo, `ps` mostra `invasore (utente, terminato)`
subito dopo che il programma è stato ucciso.
**Causa:** non è un errore. La shell ha già fatto `wait`, ma la memoria dei processi
terminati la libera il processo 0, che gira solo quando nessuno è pronto: se il
comando successivo arriva subito, non ha ancora avuto il suo turno. Scrivendo a mano
i comandi non si nota.

Il resto ha funzionato alla prima prova perché tre problemi erano stati previsti nel progetto:

| Problema previsto | Come si è evitato |
|---|---|
| Risveglio perduto: il tasto arriva tra "buffer vuoto?" e "mi blocco" | Controllo e blocco dentro la stessa sezione critica |
| Puntatore a un PCB già liberato (chi aspetta un tasto, il genitore) | Si memorizza il PID e lo si cerca con `process_find` |
| Il file viene cancellato mentre lo si sta caricando come programma | `program_run` fa ricerca e copia in un'unica sezione critica |

**Trappole classiche** (da riconoscere se ricompaiono):

| Sintomo | Causa tipica |
|---|---|
| La shell non reagisce ai tasti fino al tasto successivo | Risveglio perduto in `keyboard_read_char` |
| Ogni carattere compare due volte | Eco sia nel driver sia nella shell |
| `wait` restituisce valori a caso o va in page fault | Il PCB del figlio è stato liberato prima della `wait` (regola degli zombie) |
| Dopo `run` la shell non torna più | `process_exit` o `process_kill` non risvegliano il genitore |
| `run` di un file di testo fa cose strane | Manca il controllo del numero magico |
| Un programma legge memoria del kernel tramite un nome di file | Stringa copiata senza controllare ogni byte |

## Domande per verificare di aver capito
1. Segui il carattere `l` di `ls` dalla pressione del tasto alla comparsa a schermo:
   quali funzioni attraversa, e in quale momento la shell è "bloccata"?
2. Che differenza c'è tra `process_sleep` e `process_block`? Perché in
   `keyboard_read_char` il controllo sul buffer e il blocco stanno nella stessa sezione critica?
3. Cosa succede, passo per passo, tra `run hello` e il ritorno del prompt? Perché il
   PCB di `hello` non può essere liberato appena il programma termina?
4. La shell gira in modalità utente. Elenca le syscall che usa per eseguire `cat leggimi.txt`.
5. Perché Ctrl+C viene gestito dal driver della tastiera e non letto dalla shell come
   un carattere qualsiasi?
