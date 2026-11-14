# Fase 4 — Processi: PCB, context switch, scheduler round-robin

## In una frase
Il kernel ora esegue più flussi di codice "contemporaneamente": ogni processo ha il
suo stack, e il timer toglie la CPU a chi la sta usando per darla al successivo.

## Il problema
Finora esisteva un solo flusso di esecuzione: `kernel_main`, dall'inizio alla fine.
Ma un sistema operativo deve far girare più programmi insieme su una sola CPU, e
nessuno di essi deve poterla tenere per sempre.

Servono tre cose:
1. un modo per **ricordare a che punto era** ogni programma (il PCB e il suo stack);
2. un modo per **passare dall'uno all'altro** (il context switch);
3. qualcuno che **decida a chi tocca**, e che possa imporlo (lo scheduler, guidato dal timer).

E appena i flussi sono più di uno compare un problema nuovo: i **dati condivisi**.
Due processi che toccano la stessa struttura possono rovinarla.

## Collegamento con la teoria
- **PCB (Process Control Block)**: `struct process` in `proc/process.h`.
- **Stati di un processo** (pronto, in esecuzione, in attesa, terminato) e le
  transizioni: `enum process_state`; le transizioni avvengono in `schedule`,
  `process_sleep`, `process_exit`, `wake_sleeping_processes`.
- **Context switch**: `context_switch` in `proc/switch.asm`.
- **Scheduling round-robin, quanto di tempo, prelazione**: `proc/scheduler.c`,
  `SCHEDULER_QUANTUM` = 5 tick = 50 ms.
- **Politica e meccanismo**: `pick_next` decide *chi* (politica), `context_switch`
  esegue il cambio (meccanismo). Cambiare algoritmo significa toccare solo `pick_next`.
- **Multitasking cooperativo contro preemptive**: `process_yield` contro `scheduler_tick`.
- **Processo idle**: il processo 0, scelto solo quando nessuno è pronto.
- **Attesa attiva contro attesa passiva**: `burn_cpu` (nei test) contro `process_sleep`.
- **Race condition, sezione critica, mutua esclusione**: `interrupts_save_and_disable`
  / `interrupts_restore`, usate in `kprintf`, `kmalloc`, `pmm_alloc_page`, nella lista dei processi.
- **Processi zombie**: un processo terminato resta nella lista finché
  `process_reap_terminated` non ne libera la memoria.
- **Thread contro processi**: per ora sono thread del kernel, cioè condividono tutta
  la memoria. Lo spazio di indirizzi separato arriva nella fase 5.

## Come funziona
**Il PCB e la lista.** Ogni processo ha un `struct process` e uno stack di 8 KiB,
entrambi presi con `kmalloc`. I PCB formano una lista circolare. `kernel_main` diventa
il processo 0 ("idle") con `process_init`: non viene creato, riceve solo un PCB.

**L'idea del context switch.** Lo stato di un processo fermo sta tutto in cima al
*suo* stack. Nel PCB basta ricordare dove: il valore di `ESP`.
Cambiare processo = salvare i registri sullo stack vecchio, cambiare `ESP`,
riprendere i registri dallo stack nuovo.
```
context_switch(&vecchio->esp, nuovo->esp):

  stack del VECCHIO                       stack del NUOVO
  | ...              |                    | ...              |
  | ind. di ritorno  |                    | ind. di ritorno  | <- il `ret` torna qui
  | EBP              |  1. push           | EBP              |
  | EBX              |                    | EBX              |  3. pop
  | ESI              |                    | ESI              |
  | EDI              | <- ESP salvato     | EDI              | <- ESP caricato
                          nel PCB (2.)
```
La funzione viene chiamata da un processo e ritorna in un altro. Il vecchio resta
"congelato" dentro la chiamata finché qualcuno non tornerà a lui.

**Un processo nuovo** non ha mai chiamato `context_switch`, quindi `process_create`
gli costruisce a mano uno stack identico: quattro zeri per i registri e, come
indirizzo di ritorno, `process_trampoline`. Al primo cambio il `ret` salta lì:
il trampolino riaccende gli interrupt, chiama la funzione del processo e, quando
questa ritorna, chiama `process_exit`.

**La prelazione.** Tutto parte dal timer:
```
tick del timer (IRQ 0), mentre gira il processo A
  └─ interrupt_common salva i registri di A sullo stack di A
     └─ timer_handler → scheduler_tick
          sveglia chi ha finito di dormire, conta il tick ad A, quantum_left--
          quanto esaurito? → schedule()
               └─ pick_next() sceglie B → context_switch(A, B)
                    ...ora gira B: esce dal SUO gestore del timer con iret e continua...
```
A resta fermo a metà del gestore dell'interrupt. Quando verrà scelto di nuovo,
`context_switch` "ritornerà", il gestore finirà e `iret` lo farà ripartire.

**Come un processo lascia la CPU:** quanto scaduto (resta *pronto*);
`process_yield` (resta *pronto*); `process_sleep(n)` (va *in attesa* fino al tick giusto);
`process_exit` (diventa *terminato*).

**Le sezioni critiche.** Su una sola CPU, l'unico modo in cui un processo può essere
interrotto a metà di un'operazione è un interrupt. Quindi per proteggere i dati
condivisi basta spegnere gli interrupt per il tempo necessario:
```c
uint32_t flags = interrupts_save_and_disable();
... operazione sui dati condivisi ...
interrupts_restore(flags);
```

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|
| `proc/process.c`, `process.h` | PCB, creazione e fine dei processi | `struct process`, `process_init`, `process_create`, `process_trampoline`, `process_exit`, `process_reap_terminated`, `process_print_list` |
| `proc/switch.asm` | Cambio di contesto | `context_switch` |
| `proc/scheduler.c`, `scheduler.h` | Round-robin con prelazione | `schedule`, `pick_next`, `scheduler_tick`, `process_yield`, `process_sleep`, `wake_sleeping_processes` |
| `proc/proctest.c`, `proctest.h` | Prove automatiche | `proctest_cooperative`, `proctest_preemptive`, `burn_cpu` |
| `kernel/interrupts.c`, `.h` | (aggiunta) sezioni critiche | `interrupts_save_and_disable`, `interrupts_restore` |
| `drivers/timer.c` | (modifica) aggancio dello scheduler | `timer_handler` |
| `lib/kprintf.c` | (modifica) stampa atomica | `kprintf`, `print_formatted` |
| `mm/kheap.c`, `mm/pmm.c` | (modifica) protezione dalle race | `kmalloc`, `kfree`, `pmm_alloc_page`, `pmm_free_page` |
| `lib/string.c` | (aggiunta) funzioni sulle stringhe | `string_copy`, `string_equals` |
| `kernel/kernel.c` | (modifica) avvio dei processi, ciclo idle | `kernel_main` |

## Concetti di C incontrati
**Puntatore a funzione come "corpo" di un thread.**
```c
struct process *process_create(const char *name, void (*entry)(void));
process_create("ping", ping_thread);
```
È `new Thread(this::pingThread).start()`. `void (*entry)(void)` si legge:
"`entry` è un puntatore a una funzione senza parametri che non restituisce nulla".

**`enum`.** `enum process_state { PROCESS_READY, PROCESS_RUNNING, ... };`
Come in Java è un elenco chiuso di valori, ma in C sono solo interi con un nome
(0, 1, 2...): niente metodi, e il compilatore non impedisce di assegnare un numero qualsiasi.

**`extern` per le variabili globali.**
```c
extern struct process *current_process;   // in process.h: "esiste, altrove"
struct process *current_process = 0;      // in process.c: la definizione vera
```
Una variabile usata da più file si *definisce* in un solo `.c` e si *dichiara*
`extern` nell'header. È l'equivalente di un campo `public static`.

**Scrivere sullo stack a mano.**
```c
uint32_t *sp = (uint32_t *) ((uint8_t *) process->stack + KERNEL_STACK_SIZE);
*(--sp) = (uint32_t) process_trampoline;
```
`*(--sp) = x` fa ciò che fa `push x`: sposta il puntatore di un elemento verso il
basso, poi scrive. In Java lo stack delle chiamate è invisibile; qui lo costruiamo noi.

**L'indirizzo di un campo: `&previous->esp`.** `&` dà l'indirizzo di una variabile.
Serve quando una funzione deve *scrivere* in una variabile di chi la chiama
(in Java non si può: i parametri primitivi sono sempre copie).

**`volatile` tra processi.** Come per gli interrupt: una variabile modificata da un
altro flusso va riletta ogni volta. Vedi `finished_count` in `proc/proctest.c`.

**`x++` non è atomico.** È "leggi, somma, riscrivi": tre passi che un cambio di
processo può separare. Vale anche in Java (per questo esistono `synchronized` e
`AtomicInteger`); qui la soluzione è la sezione critica.

**Stringhe a lunghezza fissa.** `char name[16]` è un array *dentro* la struct, non un
riferimento. Copiarci una stringa più lunga scriverebbe sui campi successivi:
`string_copy` tronca e garantisce il terminatore.

## Decisioni prese
- **Thread del kernel prima dei processi utente.** Stesso meccanismo, senza cambi di
  privilegio né di spazio di indirizzi, che arrivano nella fase 5.
- **Lista circolare di PCB allocati con `kmalloc`**, invece di un array a dimensione
  fissa: nessun limite al numero di processi, ed è la forma naturale del round-robin.
- **Nel PCB si salva solo `ESP`**; i registri stanno sullo stack del processo.
  `context_switch` salva solo i 4 registri che la convenzione del C impone di conservare.
- **Quanto di 5 tick (50 ms)**, ricaricato a ogni scelta.
- **Il processo 0 è l'idle** e gira solo se nessun altro è pronto; viene interrotto a
  ogni tick, senza aspettare un quanto. Non può dormire né terminare.
- **Mutua esclusione spegnendo gli interrupt**, con salvataggio e ripristino dello stato
  per permettere sezioni annidate. Scartati semafori e lock: su una sola CPU e con
  sezioni brevissime non servono ancora.
- **`kprintf` atomica**: una riga non viene mai spezzata da un altro processo.
- **Pulizia dei terminati affidata al processo 0** (`process_reap_terminated`): un
  processo non può liberare lo stack su cui sta ancora girando.
- **`process_sleep` con controllo a ogni tick** di tutta la lista. Semplice; con molti
  processi servirebbe una coda ordinata per istante di risveglio.

## Come provarlo
```bash
make test
```
Dopo 8 secondi stampa circa 100 righe. La parte nuova:
```
Processi inizializzati: kernel_main e' il processo 0 (idle)
Prova dei processi cooperativi (yield):
  [ping] turno 1
  [pong] turno 1
  ...
  ordine dei turni: ababab
Prova della prelazione (quanto di 5 tick) e di sleep:
  [calcolo-A] passo 1 di 3
  [calcolo-B] passo 1 di 3
  [dormiglione] sveglia 1 di 3
  Elenco dei processi:
    pid 0  idle  (pronto, 3 tick di CPU)
    pid 3  calcolo-A  (pronto, 15 tick di CPU)
    pid 4  calcolo-B  (pronto, 15 tick di CPU)
    pid 5  dormiglione  (in esecuzione, 0 tick di CPU)
  ...
  ordine dei turni: ABsABsABs
  cambi di contesto durante la prova: 20
  ok: B ha fatto progressi prima che A finisse (prelazione)
  ...
Processi: tutte le prove superate
...
>>> TEST OK: avvio, eccezioni, memoria, processi e timer funzionano
```
Cosa osservare: `calcolo-A` e `calcolo-B` non cedono mai la CPU, eppure avanzano
insieme (`ABsABs`, non `AAABBB`): è la prelazione. Il dormiglione ha 0 tick di CPU.
I numeri esatti di tick possono variare di poco.
(Dalla fase 5 l'output prosegue con le prove dei programmi utente e il messaggio finale è diverso: vedi `docs/notes/fase5-modo-utente.md`.)

Con `make run` la tastiera continua a funzionare anche durante le prove.

**Esperimento:** in `proc/scheduler.h` porta `SCHEDULER_QUANTUM` a 50 e rilancia:
l'ordine dei turni cambia, perché ogni processo ora completa più passi prima di
perdere la CPU.

## Errori incontrati
**Sintomo:** `make test` falliva circa una volta su sei con
`ERRORE: prima dello yield nessuno ha ancora girato`. Le altre volte passava.
**Causa:** una race condition, ma nel *test*, non nel kernel. Il test creava due
processi e subito dopo verificava che nessuno fosse ancora partito. Era vero finché
lo scheduler non era agganciato al timer. Con la prelazione, se un tick cade proprio
tra `process_create` e il controllo, lo scheduler fa partire il nuovo processo
all'istante: il processo 0 non ha un quanto da finire.
**Soluzione:** creare i processi e fare il controllo dentro una sezione critica.
**Come è stato trovato:** un fallimento su 5 avvii; poi 60 avvii di fila salvando il
log, che hanno mostrato sempre lo stesso `ERRORE`. Dopo la correzione: 60 su 60.
**Lezione:** un bug di concorrenza non si presenta a ogni esecuzione. "Ha funzionato
una volta" non dimostra niente: bisogna ripetere molte volte e ragionare su *tutti*
i punti in cui può cadere un interrupt.

**Trappole classiche evitate** (da riconoscere se ricompaiono):

| Sintomo | Causa tipica |
|---|---|
| Triple fault al primo cambio di contesto | Stack iniziale del nuovo processo costruito male: numero o ordine dei valori diverso dai `pop` di `context_switch` |
| Un processo appena creato gira ma non viene mai interrotto | Manca `interrupts_enable()` nel trampolino |
| Dopo il primo cambio il timer non arriva più | EOI al PIC inviato *dopo* il gestore: il processo nuovo non ci passa mai (per questo lo mandiamo prima, dalla fase 2) |
| Righe di output mescolate carattere per carattere | `kprintf` non atomica |
| L'heap si rovina "a caso" con più processi | `kmalloc` interrotta a metà da un altro processo che chiama `kmalloc` |
| Crash dopo la fine di un processo | Il processo ha liberato il proprio stack mentre lo stava usando |
| Tutto fermo quando i processi dormono | Manca un processo idle sempre pronto |

## Domande per verificare di aver capito
1. Cosa contiene il PCB e perché basta salvare `ESP` per poter riprendere un processo?
2. `context_switch` salva solo quattro registri. Perché non servono gli altri? E quando
   il cambio parte da un interrupt del timer, chi ha salvato `EAX`, `ECX` ed `EDX`?
3. Segui un processo appena creato: da dove parte la sua prima istruzione e come ci arriva la CPU?
4. Che differenza c'è tra multitasking cooperativo e con prelazione? Quale prova nei
   test dimostra che la prelazione funziona davvero?
5. Perché `finished_count++` ha bisogno di una sezione critica? Perché spegnere gli
   interrupt basta a proteggerla, e in quale situazione non basterebbe più?
