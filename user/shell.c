/*
 * user/shell.c — La shell: l'interprete dei comandi.
 *
 * È un normale PROGRAMMA UTENTE, come hello: gira in modalità utente, in un proprio
 * spazio di indirizzi, e fa tutto tramite syscall. Non ha nessun potere speciale.
 * Il kernel si limita ad avviarla per prima.
 *
 * Il suo lavoro è un ciclo infinito, detto REPL (read-eval-print loop):
 *   1. stampa il prompt;
 *   2. legge una riga dalla tastiera;
 *   3. la divide in comando e argomento;
 *   4. esegue il comando;
 *   5. ricomincia.
 *
 * COLLEGAMENTO CON LA TEORIA: la shell è l'"interfaccia a riga di comando" del
 * sistema operativo. Si vede qui che NON fa parte del kernel: è un processo come
 * gli altri, e per avviare un programma usa le syscall spawn e wait.
 */

#include "user/lib/ulib.h"

#define LINE_SIZE 128       /* lunghezza massima di una riga di comando */

/* Salta gli spazi iniziali: restituisce un puntatore al primo carattere non-spazio.
 * Non copia nulla: è lo stesso array, guardato da un punto più avanti. */
static char *skip_spaces(char *text)
{
    while (*text == ' ') {
        text++;
    }
    return text;
}

/*
 * Divide `text` alla prima parola. Tecnica tipica del C: invece di creare due
 * stringhe nuove (non abbiamo `new` né `split`), si scrive uno '\0' al posto dello
 * spazio. La stringa originale risulta così "tagliata" alla prima parola, e si
 * restituisce un puntatore a ciò che segue.
 *
 *     prima:   c a t   l e g g i m i . t x t \0
 *     dopo:    c a t \0 l e g g i m i . t x t \0
 *              ^ text   ^ valore restituito
 *
 * Se non c'è altro dopo la prima parola, restituisce una stringa vuota.
 */
static char *split_first_word(char *text)
{
    while (*text != '\0' && *text != ' ') {
        text++;
    }

    if (*text == '\0') {
        return text;            /* fine della riga: il "resto" è la stringa vuota */
    }

    *text = '\0';               /* termina qui la prima parola */
    return skip_spaces(text + 1);
}

/* Stampa `text` seguito da spazi fino a occupare `width` colonne (per incolonnare). */
static void print_padded(const char *text, uint32_t width)
{
    print(text);
    for (uint32_t i = string_length(text); i < width; i++) {
        print(" ");
    }
}

/* ---------------------------------------------------------------------------
 * I comandi
 * --------------------------------------------------------------------------- */

static void command_help(void)
{
    print("Comandi disponibili:\n");
    print("  help               mostra questo elenco\n");
    print("  ls                 elenca i file\n");
    print("  cat <file>         mostra il contenuto di un file\n");
    print("  run <programma>    esegue un programma e ne attende la fine\n");
    print("  ps                 elenca i processi\n");
    print("  scrivi <file> <testo>   crea un file con quel testo\n");
    print("  rm <file>          cancella un file\n");
    print("  exit               chiude questa shell\n");
    print("Ctrl+C interrompe il programma in esecuzione.\n");
}

static void command_ls(void)
{
    char name[FILE_NAME_LENGTH];

    /* Si chiede al kernel un file alla volta, per indice, finché risponde -1. */
    for (uint32_t index = 0; ; index++) {
        int size = file_info(index, name, sizeof(name));
        if (size < 0) {
            break;
        }

        print("  ");
        print_padded(name, 18);
        print_number(size);
        print(" byte\n");
    }
}

static void command_cat(const char *name)
{
    if (name[0] == '\0') {
        print("Uso: cat <file>\n");
        return;
    }

    /*
     * Il file viene letto A PEZZI in un buffer di dimensione fissa: così funziona
     * con file di qualsiasi lunghezza usando poca memoria. `offset` ricorda fin
     * dove siamo arrivati. È lo stesso schema di un ciclo di lettura su un
     * InputStream in Java:  while ((n = in.read(buffer)) > 0) { ... }
     */
    char buffer[128];
    uint32_t offset = 0;
    char last_character = '\n';

    for (;;) {
        int count = file_read(name, buffer, sizeof(buffer), offset);

        if (count < 0) {
            print("cat: file non trovato: ");
            print(name);
            print("\n");
            return;
        }
        if (count == 0) {
            break;              /* fine del file */
        }

        write(buffer, (uint32_t) count);
        offset += (uint32_t) count;
        last_character = buffer[count - 1];
    }

    /* Se il file non termina con un a-capo lo aggiungiamo, per non ritrovarci il
     * prompt attaccato all'ultima riga. */
    if (last_character != '\n') {
        print("\n");
    }
}

static void command_run(const char *name)
{
    if (name[0] == '\0') {
        print("Uso: run <programma>\n");
        return;
    }

    /* spawn: il kernel crea un nuovo processo con quel programma. */
    int pid = spawn(name);
    if (pid < 0) {
        print("run: '");
        print(name);
        print("' non esiste oppure non e' un programma\n");
        return;
    }

    /* wait: la shell si blocca finché il programma non termina. Nel frattempo non
     * consuma CPU e non legge la tastiera: il programma è "in primo piano". */
    int exit_code = wait(pid);

    print("[programma terminato con codice ");
    print_number(exit_code);
    print("]\n");
}

static void command_scrivi(char *arguments)
{
    /* Gli argomenti sono "nome testo...": si dividono con la stessa tecnica. */
    char *name = arguments;
    char *text = split_first_word(name);

    if (name[0] == '\0' || text[0] == '\0') {
        print("Uso: scrivi <file> <testo>\n");
        return;
    }

    /* Il contenuto del file: il testo più un a-capo finale. Lo componiamo in un
     * buffer locale; il testo è al massimo lungo quanto la riga, quindi ci sta. */
    char content[LINE_SIZE + 1];
    uint32_t length = string_length(text);

    memcpy(content, text, length);
    content[length] = '\n';

    if (file_write(name, content, length + 1) < 0) {
        print("scrivi: impossibile creare il file (nome non valido o spazio esaurito)\n");
    }
}

static void command_rm(const char *name)
{
    if (name[0] == '\0') {
        print("Uso: rm <file>\n");
        return;
    }

    if (file_delete(name) < 0) {
        print("rm: file non trovato: ");
        print(name);
        print("\n");
    }
}

/* ---------------------------------------------------------------------------
 * Il ciclo principale
 * --------------------------------------------------------------------------- */

int main(void)
{
    char line[LINE_SIZE];

    print("\n=== mini-os: shell (pid ");
    print_number(getpid());
    print(") === scrivi 'help' per i comandi\n");

    for (;;) {
        print("mini-os> ");
        read_line(line, sizeof(line));

        /* Dalla riga si ricavano il comando (prima parola) e l'argomento (il resto). */
        char *command = skip_spaces(line);
        char *argument = split_first_word(command);

        if (command[0] == '\0') {
            continue;           /* riga vuota: si torna al prompt */
        }

        /* In C non si può fare `switch` su una stringa (né confrontarle con ==):
         * serve una catena di confronti con string_equals. */
        if (string_equals(command, "help")) {
            command_help();
        } else if (string_equals(command, "ls")) {
            command_ls();
        } else if (string_equals(command, "cat")) {
            command_cat(argument);
        } else if (string_equals(command, "run")) {
            command_run(argument);
        } else if (string_equals(command, "ps")) {
            ps();
        } else if (string_equals(command, "scrivi")) {
            command_scrivi(argument);
        } else if (string_equals(command, "rm")) {
            command_rm(argument);
        } else if (string_equals(command, "exit")) {
            print("Arrivederci.\n");
            return 0;
        } else {
            print("Comando sconosciuto: ");
            print(command);
            print(" (scrivi 'help' per l'elenco)\n");
        }
    }
}
