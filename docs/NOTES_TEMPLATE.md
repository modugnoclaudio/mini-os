# Modello per le NOTES di ogni fase

Ogni fase ha il suo file in `docs/notes/`, ad esempio `docs/notes/fase2-interrupt.md`.
Le NOTES sono la **dispensa** del progetto: devono permettere all'autore di
capire e spiegare a voce (es. a un esame o a un colloquio) tutto ciò che è stato costruito.

Regole di scrittura:
- Italiano semplice, frasi brevi.
- Spiega il **perché**, non solo il cosa: il codice già dice cosa fa.
- Massimo 2-3 pagine per fase: se cresce troppo, taglia i dettagli secondari.
- Cita i file e le funzioni con il loro nome esatto, così si ritrovano nel codice.

---

## Struttura (copia queste sezioni)

```markdown
# Fase N — Titolo

## In una frase
Cosa sa fare il sistema alla fine di questa fase, che prima non sapeva fare.

## Il problema
Quale problema reale risolve questa fase e perché serve al sistema operativo.

## Collegamento con la teoria
Quali argomenti del corso di Sistemi Operativi compaiono qui e dove.

## Come funziona
Il flusso spiegato passo per passo, dall'evento iniziale al risultato.
Uno schema ASCII se aiuta.

## I file di questa fase
| File | Cosa contiene | Funzioni chiave |
|---|---|---|

## Concetti di C incontrati
Ogni concetto nuovo per chi viene da Java, con un mini esempio
e il paragone con Java.

## Decisioni prese
Le scelte di progetto, le alternative scartate e il motivo.

## Come provarlo
Comandi da lanciare e cosa si deve vedere in QEMU.

## Errori incontrati
Bug trovati durante lo sviluppo, il sintomo e la causa.
(Sono la parte più istruttiva: non saltarla.)

## Domande per verificare di aver capito
3-5 domande a cui l'autore dovrebbe saper rispondere senza guardare il codice.
```

---

## Esempio di riferimento (estratto, Fase 1)

> ## In una frase
> Il computer si accende, GRUB/QEMU carica il nostro kernel e sullo schermo compare "Hello, kernel!".
>
> ## Il problema
> All'accensione la CPU non sa niente del nostro codice. Serve qualcuno che lo carichi
> in memoria e ci salti dentro: è il bootloader. Noi usiamo lo standard **Multiboot**,
> così QEMU fa da bootloader e noi dobbiamo solo rispettarne il formato.
>
> ## Concetti di C incontrati
> **Puntatore a un indirizzo fisso.**
> ```c
> volatile uint16_t *vga = (uint16_t *) 0xB8000;
> vga[0] = 'H' | (0x0F << 8);   // carattere + colore
> ```
> In Java non puoi scegliere *dove* sta un dato in memoria; in C sì. Qui diciamo:
> "tratta l'indirizzo 0xB8000 come un array di numeri a 16 bit". Quella zona di memoria
> è collegata alla scheda video: scriverci sopra fa apparire caratteri a schermo.
> `volatile` dice al compilatore di non ottimizzare via queste scritture,
> perché hanno un effetto "fuori" dal programma.
>
> ## Errori incontrati
> **Sintomo:** QEMU dice "invalid multiboot header".
> **Causa:** l'header Multiboot non era nei primi 8 KB del file, perché il linker
> l'aveva messo dopo il codice. Risolto con una sezione `.multiboot` dedicata in `linker.ld`.
>
> ## Domande per verificare di aver capito
> 1. Perché non possiamo usare `printf` della libreria standard?
> 2. Cosa succede se togli `volatile` dal puntatore alla memoria video?
> 3. Chi decide a quale indirizzo viene caricato il kernel?
