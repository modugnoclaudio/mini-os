# Fase 5 — Syscall e programmi in modalità utente

## In una frase
Il kernel ora esegue programmi veri: compilati a parte, ognuno nella propria memoria
e con la CPU in modalità non privilegiata, che parlano con il kernel solo tramite syscall.

## Il problema
I "processi" della fase 4 erano funzioni del kernel: potevano fare qualsiasi cosa,
compreso rovinare la memoria degli altri o spegnere gli interrupt. Un sistema
operativo deve invece poter eseguire codice **di cui non si fida**.

Servono tre barriere e una porta:
1. **modalità utente**: la CPU rifiuta al programma le istruzioni pericolose;
2. **memoria separata**: ogni processo vede solo la propria, non quella degli altri;
3. **kernel protetto**: il programma non può leggere né scrivere la memoria del kernel;
4. **le syscall**: l'unica porta, sorvegliata, per chiedere servizi al kernel.

## Collegamento con la teoria
- **Modo kernel e modo utente** (su x86: ring 0 e ring 3), istruzioni privilegiate:
  segmenti utente in `kernel/gdt.c`; prova in `user/privilegiato.c`.
- **System call / trap**: `int 0x80`, `kernel/syscall.c`. Il passaggio di modo è
  fatto dalla CPU, non dal programma.
- **Spazio di indirizzi per processo**: una page directory ciascuno,
  `paging_create_directory` in `mm/paging.c`.
- **Protezione della memoria**: bit `PAGE_USER` sulle pagine; chi viola viene
  terminato (`process_kill_current`). È il "Segmentation fault" di Linux.
- **Due stack per processo** (utente e kernel) e il **TSS** che dice alla CPU dove
  trovare il secondo: `gdt_set_kernel_stack`.
- **Costo del cambio di contesto**: tra processi si cambia anche `CR3` e si svuota
  il TLB; tra thread no. Vedi `schedule` in `proc/scheduler.c`.
- **Loader**: `process_create_user` costruisce lo spazio di indirizzi e vi copia il programma.
- **Validazione degli argomenti delle syscall**: `paging_is_user_range`, usata da `sys_write`.
- **Starvation**: scoperta sulla nostra pelle, vedi "Errori incontrati".

## Come funziona
**La memoria di un processo utente** (indirizzi virtuali, uguali per tutti i processi):
```
0x00000000 ┌──────────────────────────┐
           │ kernel: RAM in identità  │  presente in OGNI processo,
0x40000000 ├──────────────────────────┤  ma senza permesso utente
           │ codice e dati (64 KiB)   │  ┐
           │        ...               │  │ privato del processo
           │ stack utente (16 KiB)    │  ┘ (permesso PAGE_USER)
0xB0000000 ├──────────────────────────┤
0xD0000000 │ kernel: heap             │  presente in OGNI processo
           └──────────────────────────┘
```
La directory di un processo nasce come copia di quella del kernel: le righe del
kernel puntano alle **stesse** page table, quindi il kernel è mappato ovunque, agli
stessi indirizzi. Per questo una syscall non deve cambiare spazio di indirizzi.

**Dal sorgente al processo:**
1. `user/hello.c` viene compilato e collegato con `user/user.ld` all'indirizzo
   `0x40000000`, producendo `build/user/hello.bin` (binario piatto, solo byte).
2. `proc/user_programs.asm` lo incorpora nel kernel con `incbin`.
3. `program_run("hello")` lo trova per nome e chiama `process_create_user`, che:
   crea la directory, alloca 16 pagine azzerate e vi copia il programma, alloca 4
   pagine di stack, e prepara il PCB come per un thread.
4. Quando lo scheduler lo sceglie, `process_trampoline` chiama `enter_user_mode`.

**Scendere in modalità utente.** Non esiste un'istruzione apposita. `enter_user_mode`
(`proc/usermode.asm`) costruisce sullo stack ciò che la CPU salva per un interrupt
arrivato in modalità utente (`SS`, `ESP`, `EFLAGS`, `CS`, `EIP`) ed esegue `iret`:
la CPU "ritorna" in un programma che non era mai partito.

**Una syscall, passo per passo** (`print("ciao")` in un programma):
```
programma (ring 3)     write(): EAX=1, EBX=indirizzo, ECX=lunghezza;  int 0x80
        │
CPU                    riga 0x80 della IDT: accessibile da ring 3 → passa a ring 0,
        │              legge esp0 dal TSS, cambia stack, salva SS/ESP/EFLAGS/CS/EIP
        ▼
interrupt_stub_128 → interrupt_common → interrupt_dispatch → syscall_handler
        │              controlla il numero, chiama syscall_table[1] = sys_write
        ▼
sys_write              controlla che [indirizzo, indirizzo+lunghezza) sia memoria
        │              utente di QUESTO processo, poi stampa
        ▼
regs->eax = risultato → popa → iret → programma (ring 3), con il risultato in EAX
```

**Il cambio di processo** ora fa due cose in più (`schedule`): aggiorna `esp0` nel
TSS con la cima dello stack del kernel del processo entrante, e carica la sua
directory in `CR3` se è diversa da quella attuale.

**Quando un programma sbaglia.** Un'eccezione arrivata da ring 3 (si vede dai 2 bit
bassi di `CS` salvato) non è un panic: `process_kill_current` stampa un messaggio e
termina solo quel processo. Il resto del sistema continua.

**Le syscall disponibili** (`kernel/syscall_numbers.h`): `exit`, `write`, `getpid`,
`sleep`, `yield`.

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|
| `kernel/gdt.c`, `gdt.h` | (aggiunta) segmenti utente e TSS | `gdt_init`, `gdt_set_kernel_stack`, `struct tss_entry` |
| `kernel/idt.c`, `interrupt_stubs.asm` | (aggiunta) gate `0x80` aperto a ring 3 | `idt_init`, `interrupt_stub_128` |
| `kernel/syscall.c`, `syscall.h` | Smistamento e codice delle syscall | `syscall_handler`, `sys_write`, `sys_exit`, `syscall_table` |
| `kernel/syscall_numbers.h` | Numeri delle syscall (condiviso con i programmi) | `SYS_EXIT`, `SYS_WRITE`... |
| `kernel/interrupts.c` | (modifica) eccezioni da utente | `interrupt_came_from_user`, `interrupt_dispatch` |
| `mm/paging.c`, `paging.h` | (aggiunta) spazi di indirizzi | `paging_create_directory`, `paging_destroy_directory`, `paging_map_user_page`, `paging_switch_directory`, `paging_is_user_range`, `paging_reserve_kernel_tables` |
| `proc/process.c`, `process.h` | (aggiunta) processi utente | `process_create_user`, `map_user_region`, `process_kill_current`, `process_kill`, `process_trampoline` |
| `proc/usermode.asm` | Salto in ring 3 | `enter_user_mode` |
| `proc/scheduler.c` | (modifica) TSS e `CR3` al cambio | `schedule` |
| `proc/user_programs.asm`, `programs.c` | Programmi incorporati e loro elenco | `program_run` |
| `user/user.ld` | Linker script dei programmi | — |
| `user/lib/crt0.asm`, `ulib.c`, `ulib.h` | Avvio e libreria dei programmi | `_start`, `syscall`, `write`, `exit`, `print` |
| `user/hello.c`, `conta.c`, `invasore.c`, `privilegiato.c`, `ciclo.c` | Programmi di prova | `main` |
| `mm/memtest.c`, `proc/proctest.c` | (aggiunta) prove | `memtest_address_spaces`, `proctest_syscalls`, `proctest_user_programs` |
| `Makefile` | (aggiunta) compilazione dei programmi utente | regola `build/user/%.bin` |

## Concetti di C incontrati
**Due programmi separati che condividono solo un header.** Kernel e programmi utente
sono eseguibili diversi, collegati a indirizzi diversi. In comune hanno soltanto
`kernel/syscall_numbers.h`: dei numeri. È come un'API REST tra due applicazioni:
non ci si chiama i metodi, ci si accorda su un protocollo.

**Chi chiama `main`?** Nessuna magia: `user/lib/crt0.asm` contiene `_start`, che
chiama `main` e passa il suo valore di ritorno a `exit`. In Java lo fa la JVM; in C
lo fa un pezzetto di libreria che di solito non si vede.

**Tabella di puntatori a funzione con inizializzatori designati.**
```c
static syscall_function_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT]  = sys_exit,
    [SYS_WRITE] = sys_write,
};
syscall_table[number](a, b, c);     // chiama la funzione scelta a tempo di esecuzione
```
È il modo del C di fare ciò che in Java si farebbe con una `Map<Integer, Handler>`
o con il polimorfismo.

**Numero di elementi di un array.** `sizeof(programs) / sizeof(programs[0])` è il
`programs.length` del C. Funziona solo sugli array veri, non sui puntatori.

**Differenza tra puntatori.** `programs[i].end - programs[i].start` dà il numero di
elementi (qui byte) tra i due indirizzi.

**Un puntatore vale solo in uno spazio di indirizzi.** `0x40000000` indica memoria
diversa in ogni processo. Per questo `map_user_region` scrive nelle pagine del nuovo
processo usando il loro indirizzo *fisico* (valido nel kernel), non quello utente.
In Java un riferimento è sempre valido ovunque; qui dipende da "dove ci si trova".

**Mai fidarsi dei valori che arrivano da fuori.** Numero di syscall, puntatori,
lunghezze: sono scelti dal programma. Ogni valore va controllato prima dell'uso,
compreso l'overflow (`end < start`). In Java molti di questi controlli li fa la JVM.

**Pulizia manuale in caso di errore.** In `process_create_user`, se la memoria
finisce a metà bisogna restituire tutto ciò che era stato preso. Non c'è `finally`
né garbage collector.

**`(void) parametro;`** dichiara esplicitamente che un parametro non viene usato,
evitando l'avviso del compilatore.

## Decisioni prese
- **Kernel mappato in ogni spazio di indirizzi**, protetto dal solo bit `PAGE_USER`.
  Syscall e interrupt non cambiano `CR3`. È l'impostazione classica.
- **Page table dell'heap create in anticipo** (`paging_reserve_kernel_tables`),
  così ogni directory le condivide e l'heap può crescere dentro qualsiasi processo.
- **Programmi come binari piatti a `0x40000000`**, senza formato ELF. Il loader è
  una `memcpy`. Limite: dimensioni fisse (64 KiB di immagine, 16 KiB di stack).
- **Programmi incorporati nel kernel con `incbin`**: non esiste ancora un disco.
  Diventeranno file nella fase 6.
- **Convenzione delle syscall come Linux a 32 bit**: `int 0x80`, numero in `EAX`,
  argomenti in `EBX`, `ECX`, `EDX`, risultato in `EAX`, `-1` per errore.
- **Syscall a interrupt spenti** (interrupt gate): niente concorrenza dentro il
  kernel. Costo: una syscall lunga ritarda tutto; per questo `write` ha un limite di 4096 byte.
- **Eccezione da utente = processo terminato; dal kernel = panic.**
- **Del TSS si usano solo `ss0` ed `esp0`**; il cambio di contesto resta software.
- **`process_kill`** per eliminare un processo dall'esterno: lecito perché, senza
  lock nel kernel, un processo fermo non tiene risorse bloccate.
- **Nessuna `fork`/`exec`/`wait`** per ora: i processi li crea il kernel con `program_run`.

## Come provarlo
```bash
make test
```
Dopo 8 secondi stampa circa 150 righe. La parte nuova:
```
Prova dei programmi in modalita' utente:
  [hello] Ciao dalla modalita' utente! Il mio PID e' 6
  [conta, pid 7] contatore = 7 all'indirizzo 0x400002b8
  [conta, pid 8] contatore = 8 all'indirizzo 0x400002b8
  [conta, pid 7] contatore = 14 all'indirizzo 0x400002b8
  ...
  [invasore] write() con un indirizzo del kernel: rifiutata
  [invasore] ora provo a scrivere direttamente nel kernel...

PAGE FAULT all'indirizzo 0x100000
  scrittura in modalita' utente, permessi insufficienti
  >>> processo 9 (invasore) terminato dal kernel: eccezione 14 (Page fault) ...
  [privilegiato] provo a disabilitare gli interrupt con cli...
  >>> processo 10 (privilegiato) terminato dal kernel: eccezione 13 (General protection fault) ...
  ok: 'hello' ha finito anche se 'ciclo' non cede mai la CPU (prelazione)
  ...
Processi: tutte le prove superate
>>> TEST OK: avvio, eccezioni, memoria, processi, programmi utente e timer funzionano
```
Cosa osservare: le due copie di `conta` stampano lo **stesso indirizzo** ma valori
diversi; i `PAGE FAULT` e i `terminato dal kernel` qui sono **previsti**, non errori
(la parola da non vedere mai è `ERRORE`).

(Dalla fase 6 queste prove girano solo con `make test`; `make run` porta direttamente
alla shell, e i programmi si aggiungono in `fs/initial_files.asm`: vedi `docs/notes/fase6-fs-shell.md`.)

**Scrivere un programma nuovo:** crea `user/saluto.c` con un `main`, aggiungi
`PROGRAMMA saluto` in `proc/user_programs.asm`, le due righe `extern` e la riga
nella tabella di `proc/programs.c`; avvialo con `program_run("saluto")`.

**Vedere cosa fa la CPU:** `qemu-system-i386 -kernel build/kernel.elf -display none -d int -D build/qemu.log`
scrive in `build/qemu.log` ogni interrupt, con il livello di privilegio (`cpl=3` = utente).

## Errori incontrati
**Sintomo:** dopo aver avviato `ciclo` (un programma che gira all'infinito) il
sistema sembrava bloccato: `hello` stampava, poi più nulla.
**Causa:** *starvation* del processo 0. La prova era guidata dal processo 0, che è
l'idle e per nostra scelta gira **solo quando nessun altro è pronto**. `ciclo` è
sempre pronto: il processo 0 non riceveva più la CPU e non poteva accorgersi che
`hello` aveva finito, né eliminare `ciclo`. Il kernel non era bloccato: stava
facendo esattamente ciò che gli avevamo detto.
**Come è stato trovato:** con `-d int` QEMU mostrava centinaia di interrupt del
timer tutti con `cpl=3` e lo stesso indirizzo dentro `ciclo`: la prelazione
funzionava, era chi doveva riprendere il controllo a non essere mai scelto.
**Soluzione:** la prova è guidata da un thread del kernel normale (`supervisor_thread`),
che partecipa al round-robin.
**Lezione:** il processo idle non può fare lavoro utile. La shell della fase 6 dovrà
essere un processo normale.

**Sintomo:** `incbin: unable to get length of file 'build/user/%1.bin'`.
**Causa:** NASM non sostituisce i parametri delle macro dentro le stringhe tra
virgolette. Risolto costruendo il percorso con `%defstr` e `%strcat`.

**Sintomo:** avviso `"/*" within comment`.
**Causa:** in un commento C avevo scritto `user/*.c`: la sequenza `/*` dentro un
commento è sospetta per il compilatore. Riformulato.

**Trappole classiche evitate** (da riconoscere se ricompaiono):

| Sintomo | Causa tipica |
|---|---|
| Triple fault al primo interrupt in modalità utente | TSS non caricato (`ltr`) o `esp0` non aggiornato |
| General protection fault su `int 0x80` | Gate della IDT con privilegio 0 invece di 3 |
| Page fault alla prima istruzione del programma | Pagine mappate senza `PAGE_USER`, anche solo nella riga della directory |
| Il programma parte ma non viene mai interrotto | `EFLAGS` passato a `iret` senza il bit IF |
| Page fault nel kernel dopo aver creato un processo | Page table del kernel create dopo la copia della directory |
| I processi utente si rovinano a vicenda | `CR3` o `esp0` non aggiornati al cambio di contesto |
| EOI inviato per l'interrupt 128 | Controllo `>= 32` invece di `32..47` in `interrupt_dispatch` |

## Domande per verificare di aver capito
1. Perché un programma utente non può semplicemente chiamare `kprintf`? Elenca i
   passi che avvengono tra `int 0x80` e l'esecuzione di `sys_write`.
2. Le due copie di `conta` stampano lo stesso indirizzo per `contatore`, ma valori
   diversi. Com'è possibile?
3. A cosa serve il TSS nel nostro kernel? Cosa succederebbe se lo scheduler non
   aggiornasse `esp0`?
4. Il kernel è mappato nello spazio di indirizzi di ogni processo. Cosa impedisce a
   un programma di leggerlo? E perché `sys_write` deve comunque controllare il puntatore?
5. Perché la stessa eccezione provoca un kernel panic in un caso e la sola
   terminazione di un processo nell'altro? Come fa il kernel a distinguere?
