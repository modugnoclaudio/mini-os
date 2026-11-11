; kernel/interrupt_stubs.asm — I punti di ingresso di tutti gli interrupt.
;
; Quando arriva un interrupt la CPU interrompe il codice in esecuzione e salta
; all'indirizzo scritto nella IDT (vedi kernel/idt.c). Prima di saltare salva
; DA SOLA sullo stack solo tre cose: EFLAGS, CS ed EIP (dove riprendere dopo).
; Tutto il resto (EAX, EBX, ...) dobbiamo salvarlo noi, altrimenti il codice
; interrotto, quando riprende, si ritroverebbe i registri cambiati "per magia".
;
; COLLEGAMENTO CON LA TEORIA: è il "salvataggio del contesto" che precede ogni
; gestione di interrupt. Nella fase 4 lo stesso meccanismo farà il context switch.
;
; Serve l'assembly perché una funzione C normale termina con `ret`, mentre da un
; interrupt si esce con `iret`, e perché il C non sa salvare tutti i registri.
;
; Struttura: 48 piccoli "stub" (uno per interrupt) che annotano il proprio numero
; e saltano tutti allo stesso codice comune, il quale chiama il C.

extern interrupt_dispatch       ; funzione C in kernel/interrupts.c

; ---------------------------------------------------------------------------
; Macro per generare gli stub (una macro NASM è un modello di testo: %1 è il
; primo parametro; come un metodo che però viene "incollato" a ogni uso).
; ---------------------------------------------------------------------------
; Alcune eccezioni fanno mettere alla CPU sullo stack anche un "codice di errore",
; altre no. Per avere lo stack sempre della stessa forma, dove manca mettiamo uno 0.

%macro STUB_SENZA_ERRORE 1
interrupt_stub_%1:
    push dword 0                ; finto codice di errore
    push dword %1               ; numero dell'interrupt
    jmp interrupt_common
%endmacro

%macro STUB_CON_ERRORE 1
interrupt_stub_%1:
                                ; il codice di errore l'ha già messo la CPU
    push dword %1               ; numero dell'interrupt
    jmp interrupt_common
%endmacro

section .text

; --- Eccezioni della CPU (0-31): numeri e significato fissati da Intel ---
STUB_SENZA_ERRORE 0             ; divisione per zero
STUB_SENZA_ERRORE 1             ; debug
STUB_SENZA_ERRORE 2             ; interrupt non mascherabile
STUB_SENZA_ERRORE 3             ; breakpoint
STUB_SENZA_ERRORE 4             ; overflow
STUB_SENZA_ERRORE 5             ; superamento dei limiti (bound)
STUB_SENZA_ERRORE 6             ; istruzione non valida
STUB_SENZA_ERRORE 7             ; coprocessore non disponibile
STUB_CON_ERRORE   8             ; double fault
STUB_SENZA_ERRORE 9             ; (riservata)
STUB_CON_ERRORE   10            ; TSS non valido
STUB_CON_ERRORE   11            ; segmento non presente
STUB_CON_ERRORE   12            ; errore sul segmento di stack
STUB_CON_ERRORE   13            ; general protection fault
STUB_CON_ERRORE   14            ; page fault
STUB_SENZA_ERRORE 15            ; (riservata)
STUB_SENZA_ERRORE 16            ; errore in virgola mobile
STUB_CON_ERRORE   17            ; errore di allineamento
STUB_SENZA_ERRORE 18            ; machine check
STUB_SENZA_ERRORE 19            ; errore SIMD
STUB_SENZA_ERRORE 20            ; virtualizzazione
STUB_CON_ERRORE   21            ; control protection
STUB_SENZA_ERRORE 22            ; (riservate da qui a 28)
STUB_SENZA_ERRORE 23
STUB_SENZA_ERRORE 24
STUB_SENZA_ERRORE 25
STUB_SENZA_ERRORE 26
STUB_SENZA_ERRORE 27
STUB_SENZA_ERRORE 28
STUB_CON_ERRORE   29            ; (solo CPU AMD)
STUB_CON_ERRORE   30            ; (solo CPU AMD)
STUB_SENZA_ERRORE 31            ; (riservata)

; --- Interrupt hardware (IRQ 0-15), spostati sui numeri 32-47 (vedi drivers/pic.c) ---
STUB_SENZA_ERRORE 32            ; IRQ 0: timer
STUB_SENZA_ERRORE 33            ; IRQ 1: tastiera
STUB_SENZA_ERRORE 34
STUB_SENZA_ERRORE 35
STUB_SENZA_ERRORE 36
STUB_SENZA_ERRORE 37
STUB_SENZA_ERRORE 38
STUB_SENZA_ERRORE 39
STUB_SENZA_ERRORE 40
STUB_SENZA_ERRORE 41
STUB_SENZA_ERRORE 42
STUB_SENZA_ERRORE 43
STUB_SENZA_ERRORE 44
STUB_SENZA_ERRORE 45
STUB_SENZA_ERRORE 46
STUB_SENZA_ERRORE 47

; ---------------------------------------------------------------------------
; Codice comune a tutti gli interrupt
; ---------------------------------------------------------------------------
; Stato dello stack all'arrivo qui (in alto gli indirizzi più alti):
;
;     EFLAGS, CS, EIP      <- messi dalla CPU
;     codice di errore     <- messo dalla CPU oppure il nostro 0
;     numero interrupt     <- messo dallo stub          <- ESP
;
; Dopo i nostri push, lo stack ha esattamente la forma di `struct registers`
; (kernel/interrupts.h). L'ORDINE DEI PUSH QUI E DEI CAMPI LÀ DEVE COINCIDERE.

interrupt_common:
    pusha                       ; salva EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI (in quest'ordine)

    push ds                     ; salva i registri di segmento dei dati
    push es
    push fs
    push gs

    mov ax, 0x10                ; carica il segmento dati del kernel (GDT_KERNEL_DATA).
    mov ds, ax                  ; Ora è già così, ma dalla fase 5 gli interrupt potranno
    mov es, ax                  ; arrivare mentre gira un programma utente, che usa
    mov fs, ax                  ; segmenti diversi.
    mov gs, ax

    cld                         ; il C dà per scontato che il "flag di direzione" sia a 0

    push esp                    ; argomento per il C: ESP punta al primo dei valori salvati,
                                ; cioè è un `struct registers *`
    call interrupt_dispatch
    add esp, 4                  ; toglie l'argomento dallo stack (in cdecl lo fa chi chiama)

    pop gs                      ; ripristina tutto nell'ordine inverso
    pop fs
    pop es
    pop ds
    popa

    add esp, 8                  ; scarta numero dell'interrupt e codice di errore

    iret                        ; "interrupt return": ripristina EIP, CS ed EFLAGS e
                                ; riprende il codice interrotto come se nulla fosse

; ---------------------------------------------------------------------------
; Tabella con gli indirizzi dei 48 stub
; ---------------------------------------------------------------------------
; Dal C si vede come un array: `extern uint32_t interrupt_stub_table[48];`
; Così kernel/idt.c può riempire la IDT con un ciclo invece di 48 righe uguali.
; %rep ripete il blocco; %+ incolla il valore di i al nome dell'etichetta.

section .rodata
global interrupt_stub_table
interrupt_stub_table:
%assign i 0
%rep 48
    dd interrupt_stub_%+i
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
