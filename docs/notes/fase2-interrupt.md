# Fase 2 — GDT, IDT, eccezioni, PIC, timer, tastiera

## In una frase
Il kernel ora reagisce agli eventi: gestisce gli errori della CPU, conta il tempo
con il timer e mostra a schermo i tasti premuti, invece di eseguire solo codice dall'inizio alla fine.

## Il problema
Fino alla fase 1 il kernel era un programma che parte, stampa e si ferma.
Un sistema operativo invece passa la vita ad **aspettare eventi**:
un tasto, lo scadere di un intervallo di tempo, un errore in un programma.

Senza interrupt avremmo due problemi:
- per sapere se è stato premuto un tasto dovremmo controllare di continuo (polling), sprecando CPU;
- un errore come una divisione per zero riavvierebbe la macchina (la CPU non trova
  un gestore, genera un altro errore, poi un altro: è il "triple fault").

Inoltre il timer è la base di tutto ciò che viene dopo: senza un interrupt periodico
il kernel non può togliere la CPU a un processo (fase 4).

## Collegamento con la teoria
- **Vettore delle interruzioni**: è la IDT, `kernel/idt.c`.
- **Interruzioni sincrone e asincrone**: eccezioni (0-31, causate dall'istruzione in
  corso) contro IRQ (32-47, mandati dai dispositivi). Vedi `interrupt_dispatch`.
- **Salvataggio del contesto**: `interrupt_common` in `kernel/interrupt_stubs.asm`
  salva tutti i registri; `struct registers` è il contesto salvato.
- **Controllore delle interruzioni, priorità, mascheramento**: il PIC, `drivers/pic.c`.
- **Interrupt di clock e quanto di tempo**: `drivers/timer.c`, 100 tick al secondo.
- **I/O guidato dagli interrupt contro polling**: tastiera (`drivers/keyboard.c`)
  contro seriale (`drivers/serial.c`).
- **Segmentazione e livelli di privilegio (ring 0 / ring 3)**: la GDT, `kernel/gdt.c`.
- **Idle loop**: il ciclo con `hlt` in `kernel_main` è ciò che fa il sistema quando
  non ha nulla da eseguire.

## Come funziona
**Preparazione**, in `kernel_main`, in quest'ordine:
1. `gdt_init()` — crea i segmenti di codice e dati del kernel e li carica.
2. `interrupts_init()` — chiama `idt_init()` (riempie la IDT) e `pic_init()` (sposta gli IRQ).
3. `timer_init()` e `keyboard_init()` — registrano il proprio gestore e attivano la propria linea.
4. `interrupts_enable()` — istruzione `sti`: solo ora la CPU accetta gli interrupt.

L'ordine conta: gli interrupt si accendono per ultimi, quando tutto è al suo posto.

**Il viaggio di un interrupt** (esempio: premo un tasto):
```
tastiera ──IRQ 1──► PIC ──"interrupt 33"──► CPU
                                             │ 1. finisce l'istruzione in corso
                                             │ 2. push EFLAGS, CS, EIP; spegne gli interrupt
                                             │ 3. legge la riga 33 della IDT e ci salta
                                             ▼
interrupt_stub_33      (interrupt_stubs.asm)  push 0, push 33
interrupt_common                              pusha, push dei segmenti, push esp
                                             ▼
interrupt_dispatch(regs)   (interrupts.c)     manda l'EOI al PIC
                                              chiama handlers[33]
                                             ▼
keyboard_handler           (keyboard.c)       legge lo scancode dalla porta 0x60,
                                              lo traduce, kputchar(carattere)
                                             ▼
ritorno in interrupt_common                   pop di tutto, iret
                                             ▼
il codice interrotto riprende esattamente da dove era, senza accorgersi di nulla
```

**La GDT** ha tre righe: nulla, codice kernel (selettore `0x08`), dati kernel (`0x10`).
Entrambi i segmenti partono da 0 e coprono 4 GiB ("modello piatto"): la segmentazione
c'è ma non fa nulla. Dopo `lgdt` i registri di segmento vanno ricaricati; `CS` si
cambia solo con un salto lontano (`jmp 0x08:.flush` in `kernel/gdt_flush.asm`).

**La IDT** ha 256 righe; le prime 48 puntano ai nostri stub. Sono "interrupt gate":
la CPU spegne gli interrupt entrando e `iret` li riaccende. Quindi un gestore non
viene mai interrotto da un altro.

**Gli stub.** Ogni interrupt ha uno stub minuscolo che mette sullo stack il proprio
numero. Alcune eccezioni aggiungono un codice di errore e altre no: dove manca lo
stub mette uno 0, così lo stack ha sempre la stessa forma.

**Il PIC** va "rimappato": di fabbrica manda gli IRQ 0-7 come interrupt 8-15, che in
modalità protetta sono eccezioni della CPU. Li spostiamo su 32-47. Dopo ogni
interrupt vuole la conferma **EOI**, altrimenti non ne manda più.

**Il timer** (PIT) ha un oscillatore a 1 193 182 Hz. Con divisore 11931 genera
100 interrupt al secondo. Il gestore fa solo `ticks++`.

**La tastiera** manda uno *scancode* per ogni pressione e uno per ogni rilascio
(stesso numero con il bit 7 acceso). Due tabelle lo traducono in carattere,
con e senza Shift.

**Le eccezioni.** Se nessuno ha registrato un gestore, `panic_on_exception` stampa
i registri e ferma tutto. Fa eccezione il breakpoint (`int3`), che usiamo come prova:
stampa un messaggio e riprende.

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|
| `kernel/gdt.c`, `gdt.h` | Tabella dei segmenti | `gdt_init`, `gdt_set_entry` |
| `kernel/gdt_flush.asm` | Caricamento della GDT | `gdt_flush` |
| `kernel/idt.c`, `idt.h` | Tabella degli interrupt | `idt_init`, `idt_set_gate` |
| `kernel/interrupt_stubs.asm` | Ingresso e uscita dagli interrupt | `interrupt_stub_N`, `interrupt_common`, `interrupt_stub_table` |
| `kernel/interrupts.c`, `interrupts.h` | Smistamento ai gestori, panic | `interrupt_dispatch`, `interrupt_register_handler`, `interrupts_init`, `interrupts_enable`, `panic_on_exception`; `struct registers` |
| `drivers/pic.c`, `pic.h` | Controllore degli interrupt | `pic_init`, `pic_enable_irq`, `pic_send_eoi` |
| `drivers/timer.c`, `timer.h` | Timer a 100 Hz | `timer_init`, `timer_get_ticks`, `timer_handler` |
| `drivers/keyboard.c`, `keyboard.h` | Tastiera PS/2 | `keyboard_init`, `keyboard_handler` |
| `drivers/io.h` | (aggiunta) pausa tra comandi | `io_wait` |
| `drivers/vga.c`, `drivers/serial.c` | (aggiunta) gestione di `'\b'` | `vga_putchar`, `serial_putchar`, `serial_write_byte` |
| `kernel/kernel.c` | Inizializzazione e ciclo di attesa | `kernel_main` |

## Concetti di C incontrati
**Puntatori a funzione.**
```c
typedef void (*interrupt_handler_t)(struct registers *regs);
static interrupt_handler_t handlers[48];

handlers[33] = keyboard_handler;   // salvo l'indirizzo della funzione
handlers[33](regs);                // la chiamo attraverso il puntatore
```
In Java si userebbe un'interfaccia (`Consumer<Registers>`) e `this::metodo`.
In C il nome di una funzione *è* il suo indirizzo: si mette in una variabile e si chiama.

**`struct` e l'operatore `->`.**
```c
struct registers *regs;
regs->eip        // uguale a (*regs).eip
```
Una `struct` è una classe con soli campi pubblici. In Java ogni oggetto è già un
riferimento e si scrive sempre `.`; in C con un puntatore si usa `->`.

**`__attribute__((packed))`.** Il compilatore di solito inserisce byte vuoti tra i
campi per allinearli. Le righe di GDT e IDT hanno un formato deciso da Intel, byte per
byte: `packed` vieta il riempimento. In Java la disposizione dei campi non si controlla mai.

**Una struct "sopra" lo stack.** `struct registers` non viene mai creata con una
dichiarazione: l'assembly fa i `push`, poi passa al C l'indirizzo della cima dello
stack dicendo "trattalo come una `struct registers`". Funziona solo se l'ordine dei
campi è esattamente l'inverso dell'ordine dei push. Il compilatore non può controllarlo.

**`volatile` su una variabile condivisa.** `ticks` è modificata dal gestore
dell'interrupt, alle spalle del codice normale. `volatile` obbliga a rileggerla ogni
volta. È lo stesso significato che ha in Java per le variabili condivise tra thread.

**Operazioni sui bit.**
```c
mask & ~(1 << irq)      // azzera il bit numero irq
scancode & 0x80         // controlla il bit 7
(base >> 16) & 0xFF     // estrae i bit 16-23
```
Gli operatori sono gli stessi di Java, ma qui si usano di continuo, perché
l'hardware comunica con singoli bit.

**Variabili `static` come stato persistente.** `shift_pressed` deve ricordare il suo
valore tra un interrupt e l'altro: una variabile locale sparirebbe al ritorno della
funzione. È come un campo privato di una classe di cui esiste un solo oggetto.

**Inizializzazione degli array.** `static const char scancode_normal[128] = { 0, 27, '1', ... };`
Gli elementi non elencati valgono 0. E le globali non inizializzate partono a zero:
per questo `handlers[]` e `idt[]` nascono "vuote" senza un ciclo che le azzeri.

**Macro di NASM.** `%macro STUB_SENZA_ERRORE 1` è un modello di testo: evita di
scrivere 48 volte le stesse tre righe.

## Decisioni prese
- **Modello di memoria piatto** (segmenti da 0 a 4 GiB). La protezione la farà la
  paginazione. È ciò che fanno i sistemi operativi veri; la segmentazione "vera" è scartata.
- **GDT con sole 3 righe per ora.** Segmenti utente e TSS arriveranno nella fase 5,
  quando serviranno: aggiungerli ora sarebbe codice che non si può provare.
- **Un solo punto di smistamento in C** (`interrupt_dispatch`) con una tabella di
  gestori, invece di 48 funzioni separate. I driver si registrano da soli:
  aggiungere un dispositivo non richiede di toccare il codice degli interrupt.
- **Tutti "interrupt gate"**: niente interrupt annidati. Più semplice da ragionare;
  il costo è che un gestore lento ritarda gli altri (per questo devono essere brevi).
- **EOI inviato prima di chiamare il gestore.** È sicuro perché gli interrupt sono
  spenti fino a `iret`, ed evita un bug classico della fase 4: se il gestore del timer
  cambia processo, l'EOI "dopo" non verrebbe mandato in tempo.
- **Timer a 100 Hz** (un tick ogni 10 ms): valore tradizionale, facile da convertire in secondi.
- **Layout di tastiera americano (US).** Una sola tabella semplice. Su una tastiera
  italiana le lettere coincidono, simboli e accentate no.
- **Eco diretto dal gestore della tastiera.** Il buffer dei tasti arriverà nella fase 6
  con la shell.
- **Un'eccezione non gestita ferma il sistema** (panic) mostrando i registri.
  Quando ci saranno i processi (fase 4-5) si potrà terminare solo il processo colpevole.
- **`make test` ora controlla tre messaggi** (avvio, ritorno dall'eccezione, timer)
  e dura 8 secondi invece di 5.

## Come provarlo
```bash
make test
```
Dopo 8 secondi deve stampare:
```
Hello, kernel!
Multiboot: magic 0x2badb002, info a 0x9500
Kernel caricato da 0x100000 a 0x108920 (35104 byte)
Prova kprintf: stringa, 42, -7, 0xff, Z, 100%
GDT caricata
IDT caricata, PIC rimappato
Eccezione 3 (Breakpoint) a EIP=0x101413: gestita, si riprende
Ritorno dall'eccezione riuscito
Timer a 100 Hz
Tastiera pronta: i tasti premuti compaiono qui sotto
Interrupt abilitati
Timer: 1 s dall'avvio
Timer: 2 s dall'avvio
Timer: 3 s dall'avvio
>>> TEST OK: avvio, eccezioni e timer funzionano
```
(Gli indirizzi possono cambiare leggermente se il codice viene modificato.)

**Tastiera:** `make run`, clicca nella finestra di QEMU e scrivi. Le lettere compaiono
sotto l'ultima riga, Shift dà le maiuscole, Backspace cancella, Invio va a capo.
Le righe "Timer" compaiono una al secondo. `Ctrl+Alt+G` libera il mouse.
(Con `make run-nox` la tastiera non si può provare: lì i tasti vanno alla seriale,
che il kernel non legge.)

**Panic:** per vedere un'eccezione non gestita, aggiungi temporaneamente in
`kernel_main`, dopo `interrupts_init()`:
```c
volatile int zero = 0;
kprintf("%d", 5 / zero);
```
Deve comparire `*** KERNEL PANIC: eccezione 0 (Divisione per zero) ***` con i registri.
Poi `addr2line -e build/kernel.elf <valore di EIP>` indica la riga colpevole.

## Errori incontrati
Questa fase è andata liscia: i passi sono stati provati uno alla volta (GDT, poi IDT,
poi PIC e timer, poi tastiera), quindi ogni problema aveva un solo posto dove stare.

**Sintomo:** nel log della prova della tastiera un carattere è finito in mezzo a una riga:
`hTimer: 3 s dall'avvio` e poi `ello!`.
**Causa:** non è un bug di memoria, è **concorrenza**. Il tasto `h` è arrivato appena
prima che il ciclo principale stampasse la riga del timer: i due output si mescolano
perché `kprintf` non è protetta dagli interrupt. Per ora è innocuo (annotato nei
problemi noti); è il primo assaggio delle sezioni critiche.

**Sintomo:** con il timeout di 5 secondi `make test` rischiava di non vedere tutte le righe del timer.
**Causa:** QEMU impiega circa un secondo nel BIOS prima di avviare il kernel. Timeout portato a 8 secondi.

**Trappole classiche evitate** (utili da riconoscere se ricompaiono):

| Sintomo | Causa tipica |
|---|---|
| QEMU si riavvia in continuazione | *Triple fault*: IDT sbagliata o `struct` non `packed`. Con `-no-reboot` QEMU si chiude invece di riavviare; `-d int` mostra gli interrupt |
| Appena fatto `sti` compare un "double fault" | PIC non rimappato: il timer (IRQ 0) arriva come interrupt 8 |
| Arriva un solo tick del timer, poi niente | Manca l'EOI al PIC |
| Arriva un solo tasto, poi niente | Lo scancode non viene letto dalla porta `0x60` |
| Dopo un interrupt il kernel impazzisce | L'ordine dei `push`/`pop` in assembly non corrisponde a `struct registers`, oppure manca `add esp, 8` prima di `iret` |
| Ogni tasto compare due volte | Non si scartano gli scancode di rilascio (bit 7) |

## Domande per verificare di aver capito
1. Perché il PIC va rimappato? Cosa succederebbe al primo tick del timer senza farlo?
2. Quali registri salva la CPU da sola all'arrivo di un interrupt, e quali dobbiamo
   salvare noi? Perché serve l'assembly?
3. Che differenza c'è tra un'eccezione e un IRQ? Fai un esempio di ciascuno presente nel codice.
4. Cosa succede se un gestore di IRQ non manda l'EOI? E se `ticks` non fosse `volatile`?
5. Perché `interrupts_enable()` viene chiamata per ultima in `kernel_main`?
