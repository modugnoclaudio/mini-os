; user/lib/crt0.asm — Il codice di avvio di ogni programma utente.
;
; "crt0" è il nome tradizionale ("C runtime, parte 0"). È il vero punto di partenza
; di un programma: prepara l'ambiente, chiama `main` e, quando `main` ritorna, chiede
; al kernel di terminare il processo.
;
; PARAGONE CON JAVA: in Java è la JVM a chiamare `public static void main`. In C quel
; ruolo lo ha questo pezzetto di codice, che normalmente è fornito dalla libreria
; standard e non si vede mai. Noi non abbiamo la libreria standard, quindi eccolo.

extern main                     ; definita dal programma, in C

; Sezione dal nome speciale: user/user.ld la mette all'inizio del file, perché il
; kernel inizia l'esecuzione dal primo byte del programma.
section .text.start
global _start
_start:
    ; Il kernel ci ha già dato uno stack (ESP punta alla sua cima) e ha impostato
    ; i registri di segmento: possiamo chiamare subito il C.
    call main

    ; Il valore restituito da main è in EAX (convenzione del C). Diventa il codice
    ; di uscita del processo: exit(valore).
    mov ebx, eax                ; primo argomento della syscall
    mov eax, 0                  ; numero della syscall: SYS_EXIT (kernel/syscall_numbers.h)
    int 0x80                    ; entra nel kernel

    ; exit non ritorna mai. Se per assurdo lo facesse, restiamo fermi qui:
    ; un programma utente non può nemmeno eseguire `hlt` (è un'istruzione privilegiata).
.hang:
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
