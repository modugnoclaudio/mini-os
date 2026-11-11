# Fase 1 — Boot Multiboot, output VGA e seriale, `kprintf`

(Questa nota copre anche la Fase 0: ambiente, `Makefile`, `linker.ld`.)

## In una frase
Il computer si accende, QEMU carica il nostro kernel e "Hello, kernel!" compare
sia sullo schermo sia sulla porta seriale, stampato da una nostra `kprintf`.

## Il problema
All'accensione la CPU non sa niente del nostro codice. Serve qualcuno che lo carichi
in memoria e ci salti dentro: è il bootloader. Noi usiamo lo standard **Multiboot**,
così QEMU fa da bootloader e noi dobbiamo solo rispettarne il formato.

Poi serve un modo per **vedere** cosa fa il kernel. Senza output un kernel è una
scatola nera: ogni fase successiva si debugga con i messaggi stampati qui.
Per questo abbiamo due uscite:
- lo **schermo VGA**, che è ciò che vede l'utente;
- la **seriale COM1**, che finisce nel terminale: si copia, si salva su file e
  permette di fare test automatici (`make test`).

Infine non esiste `printf`: sta nella libreria standard, che ha bisogno di un
sistema operativo. Il sistema operativo siamo noi, quindi la scriviamo.

## Collegamento con la teoria
- **Bootstrap**: la catena BIOS → bootloader → kernel vista a lezione.
  Qui il bootloader è QEMU (opzione `-kernel`).
- **Modalità della CPU**: Multiboot ci consegna la CPU in modalità protetta a 32 bit,
  con paginazione spenta e interrupt disabilitati. Gli indirizzi sono quindi **fisici**.
- **I/O mappato in memoria contro porte di I/O**: la VGA si usa scrivendo in memoria
  (`0xB8000`), la seriale con le istruzioni `in`/`out` sulle porte (`0x3F8`).
- **Polling (attesa attiva) contro interrupt**: `serial_putchar` aspetta in un ciclo
  che il chip sia pronto. È il polling: semplice ma spreca CPU. Gli interrupt arrivano in fase 2.
- **Stack**: una funzione C non può girare senza stack. Lo prepara `_start` in assembly.
- **Layout di un programma**: le sezioni `.text`, `.rodata`, `.data`, `.bss`
  sono le stesse dei processi utente. Qui le disponiamo noi con `linker.ld`.

## Come funziona
```
QEMU (-kernel build/kernel.elf)
  │  1. legge il file ELF e cerca l'header Multiboot nei primi 8 KiB
  │  2. copia il kernel in RAM a partire da 1 MiB (0x100000)
  │  3. mette EAX = 0x2BADB002, EBX = indirizzo delle info, e salta a _start
  ▼
_start            (boot/boot.asm)
  │  4. ESP = stack_top          (stack di 16 KiB nella .bss)
  │  5. push ebx, push eax       (argomenti per il C, in ordine inverso)
  │  6. call kernel_main
  ▼
kernel_main       (kernel/kernel.c)
  │  7. serial_init()  configura COM1: 38400 baud, 8N1
  │  8. vga_init()     riempie lo schermo di spazi
  │  9. kprintf("Hello, kernel!\n")
  │ 10. controlla il magic, stampa gli indirizzi del kernel
  │ 11. halt(): ciclo infinito di hlt
  ▼
kprintf → kputchar ─┬─► vga_putchar    scrive in 0xB8000 + sposta il cursore
                    └─► serial_putchar aspetta il chip, poi outb su 0x3F8
```

**L'header Multiboot** sono tre numeri a 32 bit: magic `0x1BADB002`, flag, e un
checksum tale che la somma dei tre faccia 0. Sta nella sezione `.multiboot`, che
`linker.ld` mette per prima.

**Una casella VGA** è un numero a 16 bit: byte basso = codice ASCII, byte alto = colore.
La casella (riga, colonna) sta all'indice `riga * 80 + colonna`.
Quando si supera la riga 25, `vga_scroll` copia ogni riga in quella sopra.

**`kprintf`** scorre la stringa di formato. I caratteri normali li stampa.
Quando trova `%` guarda il carattere dopo e legge il prossimo argomento con `va_arg`.
I numeri si convertono con `% base` (ultima cifra) e `/ base` (toglie l'ultima cifra):
le cifre escono al contrario, quindi passano da un piccolo buffer.

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|
| `Makefile` | Regole di compilazione e avvio | bersagli `all`, `run`, `run-nox`, `test`, `debug`, `clean` |
| `linker.ld` | Indirizzi e ordine delle sezioni | simboli `kernel_start`, `kernel_end` |
| `boot/boot.asm` | Header Multiboot, stack, ingresso | `_start` |
| `kernel/kernel.c` | Ingresso in C | `kernel_main`, `halt` |
| `drivers/io.h` | Accesso alle porte di I/O | `inb`, `outb` |
| `drivers/vga.c`, `vga.h` | Schermo in modalità testo | `vga_init`, `vga_putchar`, `vga_scroll`, `vga_update_cursor` |
| `drivers/serial.c`, `serial.h` | Porta seriale COM1 | `serial_init`, `serial_putchar` |
| `lib/kprintf.c`, `kprintf.h` | Stampa formattata | `kprintf`, `kputchar`, `print_unsigned`, `print_signed` |

## Concetti di C incontrati
**Puntatore a un indirizzo fisso.**
```c
static volatile uint16_t *vga_buffer = (volatile uint16_t *) 0xB8000;
vga_buffer[0] = 'H' | (0x07 << 8);   // carattere + colore
```
In Java non puoi scegliere *dove* sta un dato in memoria; in C sì. Qui diciamo:
"tratta l'indirizzo 0xB8000 come un array di numeri a 16 bit".
`volatile` dice al compilatore di non eliminare queste scritture,
perché hanno un effetto "fuori" dal programma.

**Header e sorgenti.** In Java una classe ha firme e corpo insieme. In C il file `.h`
dichiara cosa esiste, il `.c` lo implementa. `#include` copia il testo dell'header.
Le righe `#ifndef ... #define ... #endif` evitano che venga copiato due volte.

**Stringhe.** Non esiste `String`. Una stringa è un array di `char` che finisce con `'\0'`.
```c
while (*s != '\0') { kputchar(*s); s++; }
```
`*s` è il carattere puntato, `s++` passa al successivo. Non c'è `.length()`.

**Tipi a dimensione esatta.** In Java `int` è sempre 32 bit. In C dipende dalla
piattaforma, quindi con l'hardware si usano `uint8_t`, `uint16_t`, `uint32_t` (`stdint.h`).
Java non ha i tipi senza segno; in C sono la norma per indirizzi e registri.

**Argomenti variabili.** `Object... args` in Java è un array vero. In C gli argomenti
extra stanno sullo stack e nessuno sa quanti sono o di che tipo: lo dice solo la
stringa di formato. Si leggono con `va_start`, `va_arg`, `va_end`.
Un `%s` a cui passi un numero non dà un'eccezione: legge memoria a caso.

**Undefined behavior.** In `print_signed` non si può fare `-value` con `-2147483648`:
l'overflow di un intero con segno in C non ha un risultato definito.
Si converte prima a `uint32_t`, dove il "giro" è garantito.

**Nessun controllo sugli indici.** Scrivere oltre la fine di un array non lancia
eccezioni: sovrascrive altra memoria. Per questo `vga_putchar` controlla riga e colonna.

**`static`.** Su una funzione o variabile globale significa "visibile solo in questo
file": è il `private` del C.

**Assembly inline.** `__asm__ volatile ("outb %%al, %%dx" : : "a"(value), "d"(port));`
inserisce un'istruzione macchina che il C non sa esprimere. `"a"` e `"d"` dicono in
quali registri mettere le variabili.

**Linker e simboli.** `extern char kernel_start[];` dichiara un nome che non è in
nessun file C: lo definisce `linker.ld`. Ci interessa solo il suo indirizzo.

## Decisioni prese
- **Kernel caricato a 1 MiB.** Sotto ci sono BIOS e memoria video. È la scelta
  standard dei kernel Multiboot. Scartato: indirizzi alti ("higher half"), che
  richiedono la paginazione (se ne riparla in fase 3).
- **Sezioni allineate a 4 KiB.** Costa qualche KiB ma semplifica la paginazione.
- **Stack di 16 KiB nella `.bss`.** Abbondante per ora, e non occupa spazio nel file.
- **Header accanto ai sorgenti, inclusi dalla radice** (`#include "drivers/vga.h"`, con `-I.`).
  Scartata una cartella `include/` separata: non è nella struttura di `PLAN.md`
  e così ogni include dice subito dove sta il file.
- **`stdint.h` e `stdarg.h` del compilatore sono ammessi.** Non fanno parte della
  libreria standard: li fornisce gcc anche in modalità `-ffreestanding`.
- **`kputchar` scrive sempre su VGA e seriale insieme.** Così i log sono identici
  e il test automatico sulla seriale dice la verità anche sullo schermo.
- **Seriale a polling, 38400 baud 8N1.** Niente interrupt finché non esiste l'IDT.
- **`-O0` e `-g`.** Codice più lento ma che in GDB segue il sorgente riga per riga.
- **Bersagli `make test` e `make run-nox` in più** rispetto ai quattro richiesti:
  servono a provare il kernel senza finestra.

## Come provarlo
Una volta sola, in WSL2:
```bash
sudo apt update && sudo apt install -y build-essential nasm qemu-system-x86 gdb
```
Poi dalla cartella del progetto:
```bash
make test
```
Deve stampare:
```
Hello, kernel!
Multiboot: magic 0x2badb002, info a 0x9500
Kernel caricato da 0x100000 a 0x108010 (32784 byte)
Prova kprintf: stringa, 42, -7, 0xff, Z, 100%
Kernel fermo (hlt). Fine della fase 1.
>>> TEST OK: messaggio trovato sulla seriale
```
- `make run` apre la finestra di QEMU: le stesse cinque righe compaiono in alto a
  sinistra, grigio su nero, e anche nel terminale. Si chiude la finestra per uscire.
- `make run-nox` mostra solo la seriale. Si esce con `Ctrl+A` e poi `X`.
- `make debug` apre GDB fermo all'inizio di `kernel_main`.

## Errori incontrati
**Sintomo:** `make test` non avviava il kernel; QEMU partiva senza l'opzione `-kernel` giusta.
**Causa:** nel `Makefile` la variabile `QEMUFLAGS := -kernel $(KERNEL) ...` era scritta
*prima* di `KERNEL := ...`. Con `:=` make calcola il valore subito, e in quel momento
`KERNEL` era ancora vuota. Risolto spostando `QEMUFLAGS` dopo `KERNEL`.
(Con `=` al posto di `:=` il valore si calcola all'uso, ma l'ordine giusto è più chiaro.)

**Sintomo:** `apt install qemu-system-x86` rispondeva "Unable to locate package".
**Causa:** l'elenco dei pacchetti era vecchio. Serve prima `apt update`.

**Sintomo:** `make test` stampava "qemu-system-i386: terminating on signal 15".
**Causa:** non è un errore. Il kernel non termina mai, quindi è `timeout` a chiudere
QEMU dopo 5 secondi. Il messaggio va su stderr e ora viene nascosto con `2>/dev/null`.

**Errore evitato in anticipo:** l'header Multiboot deve stare nei primi 8 KiB del file.
Per questo ha una sezione sua (`.multiboot`) messa per prima in `linker.ld` con `KEEP`.
Senza, QEMU risponde che non trova un header Multiboot valido.

## Domande per verificare di aver capito
1. Perché non possiamo usare `printf` della libreria standard?
2. Cosa succede se togli `volatile` dal puntatore alla memoria video?
3. Chi decide a quale indirizzo viene caricato il kernel, e perché proprio 1 MiB?
4. Perché `_start` è in assembly e non in C? Cosa mancherebbe a una funzione C?
5. Perché in `kprintf` un `%s` con un argomento intero è pericoloso, mentre in Java
   `printf("%s", 42)` funziona?
