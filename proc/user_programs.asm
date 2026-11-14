; proc/user_programs.asm — I programmi utente, incorporati dentro il kernel.
;
; IL PROBLEMA: i programmi utente sono file separati (build/user/*.bin), ma non
; abbiamo ancora un disco né un filesystem da cui leggerli.
;
; LA SOLUZIONE (provvisoria): `incbin` copia il contenuto di un file, byte per byte,
; dentro il kernel, come se fosse un grosso array di dati. Due etichette ne segnano
; inizio e fine; proc/programs.c le usa per trovare ogni programma.
; Nella fase 6 questi stessi byte diventeranno file di un filesystem in RAM.
;
; I percorsi sono relativi alla cartella da cui si lancia `make` (la radice).
; Il Makefile fa in modo che i .bin siano già pronti quando questo file viene assemblato.
;
; PER AGGIUNGERE UN PROGRAMMA: creare user/nome.c, aggiungere qui `PROGRAMMA nome`
; e una riga nella tabella di proc/programs.c.

; Macro: `PROGRAMMA hello` definisce user_program_hello_start e user_program_hello_end
; attorno al contenuto di build/user/hello.bin. (%1 = primo parametro della macro.)
%macro PROGRAMMA 1
; NASM non sostituisce %1 dentro una stringa tra virgolette, quindi il percorso
; del file va costruito in due passi:
;   %defstr trasforma il parametro in una stringa      (hello -> "hello")
;   %strcat incolla più stringhe in una sola           ("build/user/hello.bin")
%defstr NOME_PROGRAMMA %1
%strcat FILE_PROGRAMMA "build/user/", NOME_PROGRAMMA, ".bin"

global user_program_%1_start
global user_program_%1_end
user_program_%1_start:
    incbin FILE_PROGRAMMA
user_program_%1_end:
%endmacro

section .rodata                 ; dati in sola lettura del kernel

PROGRAMMA hello
PROGRAMMA conta
PROGRAMMA invasore
PROGRAMMA privilegiato
PROGRAMMA ciclo

section .note.GNU-stack noalloc noexec nowrite progbits
