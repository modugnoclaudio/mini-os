/*
 * fs/ramfs.h — Il filesystem in RAM.
 * Il codice è in fs/ramfs.c.
 *
 * Un filesystem minimale: una sola cartella (niente sottocartelle), file
 * identificati dal nome, contenuto tenuto in memoria. Allo spegnimento tutto sparisce.
 */
#ifndef FS_RAMFS_H
#define FS_RAMFS_H

#include <stdint.h>

#define FS_NAME_LENGTH   32             /* lunghezza massima del nome, terminatore compreso */
#define FS_MAX_FILES     32             /* quanti file può contenere il filesystem          */
#define FS_MAX_FILE_SIZE (64 * 1024)    /* dimensione massima di un file: 64 KiB            */

/*
 * Tutto ciò che il filesystem sa di un file: i suoi METADATI (nome, dimensione) e
 * dove si trova il contenuto.
 * COLLEGAMENTO CON LA TEORIA: è il nostro "descrittore di file", cioè la voce di
 * directory e l'i-node fusi in un'unica struttura.
 */
struct file {
    int used;                       /* 1 = questa casella contiene un file, 0 = è libera */
    char name[FS_NAME_LENGTH];
    uint8_t *data;                  /* il contenuto: un blocco dell'heap del kernel      */
    uint32_t size;                  /* quanti byte è lungo il contenuto                  */
};

/* Svuota il filesystem e vi crea i file iniziali (vedi fs/initial_files.asm). */
void fs_init(void);

/*
 * Crea il file `name` con quel contenuto; se esiste già, lo sostituisce.
 * I dati vengono COPIATI: dopo la chiamata `data` si può riutilizzare.
 * Restituisce 1 se riesce, 0 se il nome non è valido, il file è troppo grande,
 * il filesystem è pieno o manca memoria.
 */
int fs_write(const char *name, const uint8_t *data, uint32_t size);

/* Cerca un file per nome. Restituisce 0 se non esiste. Il puntatore restituito
 * resta valido solo finché il file non viene sostituito o cancellato. */
const struct file *fs_find(const char *name);

/*
 * Copia in `buffer` al massimo `size` byte del file, a partire dalla posizione
 * `offset`. Restituisce quanti byte ha copiato (0 se `offset` è alla fine o oltre),
 * oppure -1 se il file non esiste.
 */
int32_t fs_read(const char *name, uint32_t offset, uint8_t *buffer, uint32_t size);

/* Cancella un file. Restituisce 1 se esisteva, 0 altrimenti. */
int fs_delete(const char *name);

/* Per elencare i file: restituisce il file numero `index` (0 = il primo), contando
 * solo le caselle occupate. Restituisce 0 quando l'indice supera il numero di file. */
const struct file *fs_get_by_index(uint32_t index);

/* Quanti file ci sono. */
uint32_t fs_count(void);

#endif /* FS_RAMFS_H */
