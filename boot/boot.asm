; boot/boot.asm — Il primissimo codice del kernel che viene eseguito.
;
; Perché in assembly e non in C? Perché il C dà per scontate cose che all'avvio
; non esistono ancora: soprattutto lo STACK (dove vivono variabili locali e
; indirizzi di ritorno delle funzioni). Qui prepariamo lo stack e poi saltiamo nel C.
;
; Sintassi NASM (stile Intel): `mov destinazione, sorgente`.
; Il punto e virgola inizia un commento.

; ---------------------------------------------------------------------------
; 1) Header Multiboot
; ---------------------------------------------------------------------------
; Il bootloader (qui QEMU con -kernel, su un PC vero sarebbe GRUB) cerca nei primi
; 8 KiB del file tre numeri a 32 bit consecutivi. Se li trova, sa che siamo un
; kernel "Multiboot" e ci carica in memoria.

MB_MAGIC    equ 0x1BADB002          ; numero magico fissato dallo standard Multiboot 1
MB_ALIGN    equ 1 << 0              ; flag bit 0: allinea i moduli caricati a 4 KiB
MB_MEMINFO  equ 1 << 1              ; flag bit 1: passaci la mappa della memoria (servirà in fase 3)
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)  ; lo standard vuole: magic + flags + checksum == 0

; `equ` definisce una costante (come `static final int` in Java): non occupa memoria.

section .multiboot                  ; sezione dedicata: linker.ld la mette per prima nel file
align 4                             ; l'header deve stare a un indirizzo multiplo di 4
    dd MB_MAGIC                     ; `dd` = "define doubleword": scrive un numero a 32 bit
    dd MB_FLAGS
    dd MB_CHECKSUM

; ---------------------------------------------------------------------------
; 2) Lo stack del kernel
; ---------------------------------------------------------------------------
; Riserviamo 16 KiB nella sezione .bss (dati non inizializzati).
; Su x86 lo stack cresce VERSO IL BASSO (verso indirizzi più piccoli):
; ogni `push` diminuisce ESP. Quindi ESP deve partire dalla CIMA (stack_top).
;
;   indirizzi alti   stack_top    <- ESP all'inizio
;                    |  ....  |      (push: ESP scende)
;   indirizzi bassi  stack_bottom

section .bss
align 16
stack_bottom:
    resb 16384                      ; `resb` = riserva N byte senza inizializzarli
stack_top:

; ---------------------------------------------------------------------------
; 3) Punto di ingresso
; ---------------------------------------------------------------------------
section .text
global _start                       ; rende _start visibile al linker (vedi ENTRY in linker.ld)
extern kernel_main                  ; funzione definita in C, in kernel/kernel.c

_start:
    ; Stato della CPU garantito da Multiboot in questo momento:
    ;   - modalità protetta a 32 bit, paginazione spenta
    ;   - interrupt disabilitati
    ;   - EAX = 0x2BADB002 (prova che ci ha caricati un bootloader Multiboot)
    ;   - EBX = indirizzo di una struttura con le informazioni sul sistema
    ; Lo stack invece NON è garantito: lo impostiamo noi.

    mov esp, stack_top              ; ESP (stack pointer) = cima del nostro stack

    ; Chiamiamo kernel_main(magic, info).
    ; Convenzione di chiamata del C a 32 bit ("cdecl"): gli argomenti si mettono
    ; sullo stack in ordine INVERSO (l'ultimo per primo).
    push ebx                        ; 2° argomento: puntatore alle info Multiboot
    push eax                        ; 1° argomento: numero magico
    call kernel_main                ; salta nel C (e salva sullo stack l'indirizzo di ritorno)

    ; kernel_main non dovrebbe mai ritornare. Se succede, fermiamo la CPU:
    ; non c'è un sistema operativo a cui "tornare", il sistema operativo siamo noi.
    cli                             ; disabilita gli interrupt
.hang:
    hlt                             ; ferma la CPU fino al prossimo interrupt
    jmp .hang                       ; se si sveglia comunque, la rifermiamo

; Dichiara che questo file non ha bisogno di uno stack eseguibile
; (serve solo a evitare un avviso del linker).
section .note.GNU-stack noalloc noexec nowrite progbits
