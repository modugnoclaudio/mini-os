/*
 * fs/ramfs.c — Un filesystem in RAM.
 *
 * COS'È UN FILESYSTEM: il modo in cui il sistema operativo organizza i dati
 * permanenti, dando loro un NOME. Risponde a tre domande: quali file esistono?
 * dove sta il contenuto di ciascuno? quanto è lungo?
 *
 * LA NOSTRA VERSIONE:
 *   - la "directory" è una tabella di FS_MAX_FILES caselle (`files`);
 *   - ogni casella occupata descrive un file: nome, dimensione, puntatore ai dati;
 *   - i dati di ogni file sono un unico blocco preso dall'heap con kmalloc.
 *
 *        tabella `files`                      heap del kernel
 *      +---+---------------+------+------+
 *      | 1 | benvenuto.txt |  118 |  *---+--> "Benvenuto in mini-os!..."
 *      | 1 | hello         |  584 |  *---+--> (byte del programma)
 *      | 0 |   (libera)    |      |      |
 *      |...|               |      |      |
 *      +---+---------------+------+------+
 *
 * COLLEGAMENTO CON LA TEORIA:
 *   - directory a un solo livello ("flat"), come nei primissimi sistemi operativi;
 *   - allocazione CONTIGUA: ogni file occupa un solo blocco. Semplice e veloce da
 *     leggere, ma un file non può crescere sul posto: per modificarlo lo si
 *     riscrive da capo. (I filesystem veri spezzano i file in blocchi sparsi e
 *     tengono un indice, come la FAT o gli i-node.)
 *   - manca la cosa più importante di un filesystem vero: la PERSISTENZA. I dati
 *     stanno in RAM e si perdono allo spegnimento.
 *
 * PARAGONE CON JAVA: è in sostanza una `Map<String, byte[]>` scritta a mano, con
 * una ricerca lineare al posto della tabella hash.
 */

#include <stdint.h>
#include "fs/ramfs.h"
#include "mm/kheap.h"
#include "kernel/interrupts.h"
#include "lib/kprintf.h"
#include "lib/string.h"

/* La directory: essendo `static` e non inizializzata parte tutta a zero,
 * cioè con tutte le caselle libere (used = 0). */
static struct file files[FS_MAX_FILES];

/*
 * Una riga della tabella dei file iniziali, costruita in fs/initial_files.asm:
 * tre indirizzi a 32 bit. I campi sono nello stesso ordine in cui la macro
 * FILE_INIZIALE li scrive.
 */
struct initial_file {
    const char *name;
    const uint8_t *start;       /* primo byte del contenuto              */
    const uint8_t *end;         /* primo byte DOPO la fine del contenuto */
};

/* Le due etichette che delimitano la tabella. Sono solo indirizzi: dichiararle come
 * array permette di usarne il nome come puntatore al primo elemento. */
extern const struct initial_file initial_files_table[];
extern const struct initial_file initial_files_table_end[];

/* Un nome è valido se non è vuoto, non è troppo lungo e contiene solo caratteri
 * stampabili diversi dallo spazio. Restituisce 1 se va bene. */
static int name_is_valid(const char *name)
{
    uint32_t length = 0;

    while (name[length] != '\0') {
        char c = name[length];

        /* In ASCII i caratteri stampabili vanno da 33 ('!') a 126 ('~');
         * lo spazio è 32, sotto ci sono i caratteri di controllo. */
        if (c < 33 || c > 126) {
            return 0;
        }

        length++;
        if (length >= FS_NAME_LENGTH) {     /* non resterebbe posto per il terminatore */
            return 0;
        }
    }

    return length > 0;
}

/* Cerca la casella del file con quel nome. Versione interna: restituisce un
 * puntatore modificabile (quella pubblica, fs_find, lo restituisce `const`). */
static struct file *find_entry(const char *name)
{
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && string_equals(files[i].name, name)) {
            return &files[i];       /* &files[i] = l'indirizzo dell'elemento i dell'array */
        }
    }
    return 0;
}

/* Cerca una casella libera. Restituisce 0 se la tabella è piena. */
static struct file *find_free_entry(void)
{
    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            return &files[i];
        }
    }
    return 0;
}

void fs_init(void)
{
    /* Quante righe ha la tabella: la differenza tra due puntatori allo stesso
     * array dà il numero di ELEMENTI tra loro (non di byte). */
    uint32_t count = (uint32_t) (initial_files_table_end - initial_files_table);

    for (uint32_t i = 0; i < count; i++) {
        const struct initial_file *entry = &initial_files_table[i];
        uint32_t size = (uint32_t) (entry->end - entry->start);

        if (!fs_write(entry->name, entry->start, size)) {
            kprintf("File system: ERRORE, impossibile creare il file iniziale %s\n", entry->name);
        }
    }

    kprintf("File system in RAM pronto: %d file\n", fs_count());
}

int fs_write(const char *name, const uint8_t *data, uint32_t size)
{
    if (!name_is_valid(name) || size > FS_MAX_FILE_SIZE) {
        return 0;
    }

    /*
     * Prima si prepara il contenuto nuovo, e solo se tutto va bene si tocca la
     * tabella: così un errore a metà (memoria finita) lascia il filesystem
     * esattamente com'era, senza un file "mezzo scritto".
     * Un file vuoto (size 0) non ha bisogno di memoria: `data` resta 0.
     */
    uint8_t *copy = 0;
    if (size > 0) {
        copy = (uint8_t *) kmalloc(size);
        if (copy == 0) {
            return 0;
        }
        memcpy(copy, data, size);
    }

    /* SEZIONE CRITICA: la tabella è condivisa tra tutti i processi. */
    uint32_t flags = interrupts_save_and_disable();

    struct file *entry = find_entry(name);
    uint8_t *old_data = 0;

    if (entry != 0) {
        /* Il file esiste già: il contenuto vecchio verrà liberato qui sotto. */
        old_data = entry->data;
    } else {
        entry = find_free_entry();
        if (entry == 0) {
            /* Tabella piena: si restituisce la memoria appena presa. */
            interrupts_restore(flags);
            kfree(copy);
            return 0;
        }
        entry->used = 1;
        string_copy(entry->name, name, FS_NAME_LENGTH);
    }

    entry->data = copy;
    entry->size = size;

    interrupts_restore(flags);

    kfree(old_data);        /* kfree(0) non fa nulla */
    return 1;
}

const struct file *fs_find(const char *name)
{
    return find_entry(name);
}

int32_t fs_read(const char *name, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    uint32_t flags = interrupts_save_and_disable();

    const struct file *file = find_entry(name);
    if (file == 0) {
        interrupts_restore(flags);
        return -1;
    }

    /* Quanti byte restano dalla posizione richiesta alla fine del file. */
    uint32_t available = 0;
    if (offset < file->size) {
        available = file->size - offset;
    }

    /* Non si copia mai più di quanto chiesto né più di quanto c'è: è il controllo
     * che impedisce di leggere oltre la fine del file (in Java lo farebbe
     * l'eccezione sugli indici; qui dobbiamo pensarci noi). */
    uint32_t to_copy = size;
    if (to_copy > available) {
        to_copy = available;
    }

    if (to_copy > 0) {
        /* `file->data + offset` = l'indirizzo del byte numero `offset` del file. */
        memcpy(buffer, file->data + offset, to_copy);
    }

    interrupts_restore(flags);
    return (int32_t) to_copy;
}

int fs_delete(const char *name)
{
    uint32_t flags = interrupts_save_and_disable();

    struct file *entry = find_entry(name);
    if (entry == 0) {
        interrupts_restore(flags);
        return 0;
    }

    uint8_t *old_data = entry->data;

    /* Cancellare = liberare la casella. Il nome resta scritto, ma con used = 0 la
     * casella viene ignorata e sarà riutilizzata dal prossimo file. */
    entry->used = 0;
    entry->data = 0;
    entry->size = 0;

    interrupts_restore(flags);

    kfree(old_data);
    return 1;
}

const struct file *fs_get_by_index(uint32_t index)
{
    /* Le caselle occupate non sono per forza le prime: dopo qualche cancellazione
     * ci sono dei buchi. Si contano quindi solo quelle in uso. */
    uint32_t seen = 0;

    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) {
            if (seen == index) {
                return &files[i];
            }
            seen++;
        }
    }
    return 0;
}

uint32_t fs_count(void)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) {
            count++;
        }
    }
    return count;
}
