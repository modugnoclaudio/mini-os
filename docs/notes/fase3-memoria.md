# Fase 3 — Memoria: allocatore fisico, paginazione, heap `kmalloc`

## In una frase
Il kernel ora sa quanta RAM c'è, la distribuisce a pagine, usa indirizzi virtuali
tradotti dalla CPU e ha una sua `kmalloc` per creare oggetti di qualsiasi dimensione.

## Il problema
Fino alla fase 2 tutta la memoria del kernel era fissata al momento della
compilazione: variabili globali e uno stack. Non si poteva creare nulla "al volo".
Ma processi, file e buffer nascono e muoiono mentre il sistema gira.

Servono tre cose, una sopra l'altra:
1. **sapere quale RAM è libera** e darla a chi la chiede (allocatore fisico);
2. **decidere chi vede cosa**: ogni processo dovrà avere la sua memoria, senza poter
   toccare quella degli altri né quella del kernel (paginazione);
3. **dare pezzi piccoli**: una pagina intera da 4 KiB è troppo per una struttura
   da 40 byte (heap).

## Collegamento con la teoria
- **Gestione dello spazio libero con bitmap**: `mm/pmm.c`, un bit per pagina fisica.
- **Paginazione, tabella delle pagine a due livelli, MMU**: `mm/paging.c`.
  Indirizzo virtuale = indice nella directory + indice nella tabella + spostamento.
- **TLB**: la cache delle traduzioni. Va invalidata con `invlpg` quando si cambia
  una riga (`paging_map_page`, `paging_unmap_page`).
- **Page fault**: eccezione 14, `page_fault_handler`. Nei sistemi veri è la base di
  demand paging, swap e copy-on-write; da noi per ora segnala solo un bug.
- **Allocazione dinamica, first fit, frammentazione esterna e interna**: `mm/kheap.c`.
  La fusione dei blocchi liberi (`merge_free_blocks`) combatte la frammentazione esterna.
- **Spazio di indirizzi**: per ora ne esiste uno solo, quello del kernel. Nella fase 5
  ogni processo avrà la sua page directory.

## Come funziona
I tre livelli, dal basso verso l'alto:
```
kmalloc(40)                    "dammi 40 byte"                 mm/kheap.c
    │  se l'heap è pieno: heap_grow()
    ▼
paging_map_page(virt, fis)     "questa pagina virtuale         mm/paging.c
    │                           corrisponde a quella fisica"
    ▼
pmm_alloc_page()               "dammi una pagina fisica         mm/pmm.c
                                libera da 4 KiB"
```

**1. Allocatore fisico (PMM).** Il bootloader ci lascia la *mappa della memoria*:
l'elenco delle zone di RAM libere e riservate. `pmm_init` la legge e prepara una
**bitmap**: un bit per ogni pagina fisica, 1 = occupata. Si parte con tutto occupato
e si liberano solo le zone dichiarate libere; poi si rioccupano il primo MiB
(BIOS, video) e il kernel stesso. `pmm_alloc_page` cerca il primo bit a 0, lo mette
a 1 e restituisce l'indirizzo: numero della pagina × 4096.

**2. Paginazione.** Accesa la paginazione, ogni indirizzo usato dal codice passa
dalle tabelle prima di arrivare alla RAM:
```
indirizzo virtuale 0xD0000518
  = 1101000000 | 0000000000 | 010100011000
      832            0           0x518
       │             │             │
CR3 ─► page directory[832] ─► page table[0] ─► pagina fisica + 0x518
```
`paging_init` crea una page directory e mappa tutta la RAM **in identità**
(virtuale = fisico), così il kernel continua a funzionare identico dopo l'accensione.
Poi carica l'indirizzo della directory in `CR3` e accende il bit 31 di `CR0`.
La **pagina 0 non è mappata**: un puntatore nullo provoca un page fault.

**3. Heap.** Vive agli indirizzi virtuali da `0xD0000000`. All'inizio è vuoto.
Quando serve spazio, `heap_grow` chiede pagine al PMM e le mappa una dopo l'altra:
in RAM possono essere sparse, ma virtualmente sono contigue. Lo spazio è diviso in
blocchi, ognuno preceduto da un header di 16 byte (dimensione, libero/occupato,
puntatore al blocco successivo).
- `kmalloc`: cerca il primo blocco libero abbastanza grande (first fit); se è troppo
  grande lo taglia in due (`split_block`); restituisce l'indirizzo subito dopo l'header.
- `kfree`: torna indietro di un header, segna il blocco libero e fonde i blocchi
  liberi adiacenti (`merge_free_blocks`).

**Le prove.** `mm/memtest.c` esercita i tre livelli all'avvio e stampa `ok` o
`ERRORE` per ogni verifica. `make test` fallisce se compare un `ERRORE`.

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|
| `kernel/multiboot.h` | Strutture lasciate dal bootloader | `struct multiboot_info`, `struct multiboot_mmap_entry` |
| `mm/pmm.c`, `pmm.h` | Allocatore di pagine fisiche | `pmm_init`, `pmm_alloc_page`, `pmm_free_page`, `pmm_get_free_pages` |
| `mm/paging.c`, `paging.h` | Tabelle delle pagine | `paging_init`, `paging_map_page`, `paging_unmap_page`, `paging_get_physical`, `page_fault_handler` |
| `mm/kheap.c`, `kheap.h` | Heap del kernel | `kmalloc`, `kfree`, `heap_grow`, `split_block`, `merge_free_blocks`, `kheap_dump` |
| `mm/memtest.c`, `memtest.h` | Prove automatiche | `memtest_pmm`, `memtest_paging`, `memtest_heap`, `memtest_summary` |
| `lib/string.c`, `string.h` | Operazioni su blocchi di memoria | `memset`, `memcpy` |
| `kernel/interrupts.c` | (modifica) panic richiamabile da fuori | `interrupt_panic` |
| `kernel/kernel.c` | (modifica) inizializzazione della memoria | `kernel_main` |

## Concetti di C incontrati
**`void *`, il puntatore senza tipo.**
```c
void *kmalloc(uint32_t size);
uint8_t *buffer = (uint8_t *) kmalloc(40);
```
`void *` è "un indirizzo e basta". `kmalloc` non sa cosa metterai in quella memoria:
lo decide chi chiama, con un cast. In Java `new` conosce sempre il tipo.

**Gestione manuale della memoria.** Non c'è garbage collector. Ogni `kmalloc` vuole
il suo `kfree`. Tre bug che in Java non esistono:
- *memory leak*: dimenticare `kfree`, la memoria è persa;
- *double free*: liberare due volte lo stesso blocco;
- *use after free*: usare un blocco dopo averlo liberato. Non dà errore: rovina dati altrui.

**Aritmetica dei puntatori.**
```c
struct block_header *block;
block + 1                  // avanza di UN HEADER intero (16 byte), non di 1 byte
(uint8_t *) block + 16     // per contare in byte bisogna convertire a puntatore a byte
```
Sommare `n` a un puntatore lo sposta di `n` elementi del tipo puntato. È ciò che
permette `(void *) (block + 1)` in `kmalloc` e `(struct block_header *) pointer - 1` in `kfree`.

**Una struct "appoggiata" sulla memoria.**
```c
struct block_header *new_block = (struct block_header *) region_start;
new_block->size = ...;
```
Non si crea nessun oggetto: si dice "tratta i byte a questo indirizzo come un header".
Lo stesso trucco legge la mappa Multiboot e le tabelle delle pagine.

**Struct che punta a se stessa.** `struct block_header *next;` dentro
`struct block_header`: è una lista concatenata, come `class Nodo { Nodo next; }`.

**Puntatore nullo.** `0` come puntatore significa "niente", come `null`. Ma usarlo non
lancia un'eccezione del linguaggio: è la *nostra* paginazione (pagina 0 non mappata)
a trasformarlo in un page fault. Senza, leggerebbe i dati del BIOS in silenzio.

**Interi senza segno e "giro".** `end < start` dopo una somma rivela che il risultato
ha superato i 32 bit ed è ripartito da zero. Per gli `unsigned` è un comportamento
definito e si può usare per i controlli (`pmm_init`).

**Operazioni sui bit per impacchettare i dati.** Una riga di tabella contiene insieme
un indirizzo (20 bit alti) e dei permessi (12 bit bassi):
`entry & 0xFFFFF000` estrae il primo, `entry & PAGE_PRESENT` controlla un permesso.

**Funzioni che il compilatore dà per scontate.** gcc può inserire da solo chiamate a
`memset` e `memcpy`: per questo le nostre hanno esattamente quei nomi.

## Decisioni prese
- **Bitmap di dimensione fissa nella `.bss`** (32 KiB, copre 1 GiB). Scartato: calcolarne
  la dimensione e piazzarla "da qualche parte" in RAM, più efficiente ma più delicato.
- **RAM gestita fino a 1 GiB.** Oltre viene ignorata. Semplifica bitmap e mappatura.
- **Primo MiB mai distribuito**, anche dove risulterebbe libero. Lì stanno BIOS, video
  e la struttura Multiboot. Si perde meno di 1 MiB e si evitano controlli caso per caso.
- **Mappatura in identità di tutta la RAM** invece del kernel in alto ("higher half").
  Il kernel può scrivere in qualunque pagina fisica usandone l'indirizzo: le page table
  si modificano senza trucchi. Costo: 1 pagina di tabelle ogni 4 MiB di RAM.
- **Pagina 0 non mappata**, per catturare i puntatori nulli.
- **Disposizione degli indirizzi virtuali:** `0`–`1 GiB` kernel (identità);
  `0xD0000000`–`0xD1000000` heap del kernel. La zona in mezzo è libera per i
  programmi utente della fase 5.
- **Heap a lista di blocchi con first fit**, header di 16 byte con numero magico.
  Scartati best fit e allocatori a classi di dimensione (slab): più efficienti, meno leggibili.
- **L'heap cresce a richiesta e non si restringe mai.** Le pagine prese non tornano al PMM.
- **Il page fault è sempre un panic** per ora, con indirizzo e causa stampati.
- **Prove all'avvio** (`mm/memtest.c`) controllate da `make test`: sono il nostro JUnit.

## Come provarlo
```bash
make test
```
Dopo 8 secondi stampa circa 60 righe. Le più importanti:
```
Mappa della memoria fornita dal bootloader:
  da 0x0 per 639 KiB: libera
  ...
  da 0x100000 per 129920 KiB: libera
PMM: 32462 pagine libere (126 MiB), la RAM finisce a 0x7fe0000
Prova dell'allocatore fisico:
  ok: ...
Paginazione accesa: RAM mappata in identita' fino a 0x7fe0000, pagina 0 esclusa
Prova della paginazione:
  scritto 0xcafe1234 a 0xe0000000, letto 0xcafe1234 a 0x133000
  ok: due indirizzi virtuali vedono la stessa pagina fisica
  ...
Prova dell'heap:
  heap da 0xd0000000 a 0xd0001000:
    blocco a 0xd0000010: 40 byte, occupato
    ...
Memoria: tutte le prove superate
...
>>> TEST OK: avvio, eccezioni, memoria e timer funzionano
```
Tutte le righe di prova devono iniziare con `ok:`. Nessuna riga deve contenere `ERRORE`.

Con `make run` le righe sono più delle 25 dello schermo: le prime scorrono via.
È normale; l'output completo si legge nel terminale.

**Vedere un page fault.** Aggiungi temporaneamente in `kernel_main`, prima di `memtest_summary()`:
```c
volatile int *null_pointer = 0;
kprintf("%d", *null_pointer);
```
Deve comparire `PAGE FAULT all'indirizzo 0x0 ... quasi certamente un puntatore nullo`,
seguito dal kernel panic.

**Altre quantità di RAM:** `qemu-system-i386 -kernel build/kernel.elf -serial stdio -m 512`
(provato con 16, 128, 512 e 2048 MiB: con 2048 se ne usa 1 GiB, come previsto).

## Errori incontrati
Anche questa fase non ha prodotto bug veri: ogni livello è stato provato da solo
prima di costruirci sopra il successivo. Gli errori qui sotto sono stati **provocati
apposta** per verificare che il kernel li riconosca.

**Provocato:** lettura attraverso un puntatore nullo.
**Risultato:** page fault all'indirizzo 0 e panic con `EIP` dell'istruzione colpevole.
Senza la pagina 0 esclusa, avrebbe letto dati del BIOS senza alcun avviso.

**Provocato:** `kfree` due volte sullo stesso blocco, e `kfree` di una variabile locale.
**Risultato:** `HEAP: ERRORE ... double free` e `... non e' un blocco valido`; `make test` fallisce.
**Limite:** il riconoscimento si basa sull'header rimasto in memoria. Se nel frattempo
quella zona è stata riassegnata e sovrascritta, il doppio `kfree` può non essere visto.

**Trappole classiche evitate** (da riconoscere se ricompaiono):

| Sintomo | Causa tipica |
|---|---|
| QEMU si riavvia appena si accende la paginazione | Il codice in esecuzione, lo stack o la memoria video non sono mappati |
| Dopo aver cambiato una mappatura si legge ancora la pagina vecchia | Manca `invlpg`: il TLB ha in cache la traduzione precedente |
| Mappature "fantasma" o page fault casuali | Page table non azzerata dopo `pmm_alloc_page` |
| Il kernel si rovina da solo dopo un po' | Il PMM ha distribuito una pagina del kernel: `kernel_end` non riservato |
| Il ciclo sulla mappa della memoria legge dati senza senso | Si avanza di `size` invece che di `size + 4` |
| `undefined reference to memset` | gcc l'ha inserita da solo; serve `lib/string.c` |
| `kmalloc` restituisce blocchi che si sovrappongono | Errore nell'aritmetica dei puntatori: somma in header invece che in byte |

## Domande per verificare di aver capito
1. Perché servono sia `pmm_alloc_page` sia `kmalloc`? Cosa fa l'una che l'altra non fa?
2. Dato l'indirizzo virtuale `0xD0000518`, come lo scompone la CPU e quali tabelle legge?
3. Perché tutta la RAM è mappata in identità? Cosa succederebbe accendendo la
   paginazione senza mappare il codice del kernel?
4. Come fa `kfree` a sapere quanto è grande il blocco da liberare, se riceve solo un puntatore?
5. Cos'è la frammentazione esterna, e quale funzione del nostro heap la riduce?
