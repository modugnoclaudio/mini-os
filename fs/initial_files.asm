; fs/initial_files.asm — Il contenuto iniziale del filesystem, incorporato nel kernel.
;
; IL PROBLEMA: non abbiamo un disco. Da dove arrivano i file che si trovano
; all'avvio (i programmi utente, i file di testo)?
;
; LA SOLUZIONE: `incbin` copia il contenuto di un file del nostro computer, byte per
; byte, dentro il kernel, come un grosso blocco di dati. All'avvio fs_init()
; (fs/ramfs.c) scorre la tabella qui sotto e crea un file del filesystem in RAM per
; ogni voce. È l'idea, in piccolo, del "ramdisk iniziale" (initrd) di Linux.
;
; I percorsi sono relativi alla cartella da cui si lancia `make` (la radice).
; Il Makefile fa in modo che i programmi siano già compilati quando questo file
; viene assemblato.
;
; PER AGGIUNGERE UN FILE basta UNA riga in fondo:
;     FILE_INIZIALE "nome-nel-filesystem", "percorso/del/file/sul/computer"
; Per un programma nuovo: creare user/nome.c e aggiungere
;     FILE_INIZIALE "nome", "build/user/nome.bin"

; ---------------------------------------------------------------------------
; La macro FILE_INIZIALE
; ---------------------------------------------------------------------------
; Per ogni file produce due cose, in due sezioni diverse:
;
;   nella sezione .rodata:            il nome (stringa terminata da 0) e i byte del file
;   nella sezione .rodata.initfiles:  una riga di tabella con tre indirizzi:
;                                     nome, inizio dei dati, fine dei dati
;
; Tutte le righe di tabella finiscono una dopo l'altra nella stessa sezione: viste
; dal C sono un array di `struct initial_file` (vedi fs/ramfs.c).
;
; Le etichette che iniziano con %% sono "locali alla macro": NASM dà loro un nome
; diverso a ogni uso, così la macro si può richiamare più volte senza conflitti.
; %1 e %2 sono il primo e il secondo parametro.
%macro FILE_INIZIALE 2
section .rodata
%%nome:
    db %1, 0                    ; `db` = scrive dei byte: la stringa, poi lo 0 finale
%%inizio:
    incbin %2
%%fine:

section .rodata.initfiles
    dd %%nome                   ; `dd` = scrive un numero a 32 bit: qui un indirizzo
    dd %%inizio
    dd %%fine
%endmacro

; Inizio della tabella. Passare a una sezione già usata NON la ricomincia da capo:
; si continua a scrivere dopo ciò che c'era già.
section .rodata.initfiles
global initial_files_table
initial_files_table:

; ---------------------------------------------------------------------------
; L'elenco dei file
; ---------------------------------------------------------------------------
; File di testo: stanno nella cartella fs/root/.
FILE_INIZIALE "benvenuto.txt", "fs/root/benvenuto.txt"
FILE_INIZIALE "leggimi.txt",   "fs/root/leggimi.txt"

; Programmi utente: compilati da user/ (file .c) in build/user/.
FILE_INIZIALE "shell",         "build/user/shell.bin"
FILE_INIZIALE "hello",         "build/user/hello.bin"
FILE_INIZIALE "conta",         "build/user/conta.bin"
FILE_INIZIALE "invasore",      "build/user/invasore.bin"
FILE_INIZIALE "privilegiato",  "build/user/privilegiato.bin"
FILE_INIZIALE "ciclo",         "build/user/ciclo.bin"

; Fine della tabella.
section .rodata.initfiles
global initial_files_table_end
initial_files_table_end:

section .note.GNU-stack noalloc noexec nowrite progbits
