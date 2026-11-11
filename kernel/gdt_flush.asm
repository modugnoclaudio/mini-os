; kernel/gdt_flush.asm — Carica la nuova GDT nella CPU.
;
; Chiamata dal C come:  gdt_flush(indirizzo_di_gdt_ptr)
;
; Caricare la tabella con `lgdt` non basta: i registri di segmento (CS, DS, SS...)
; tengono in una "cache" interna la riga della VECCHIA tabella. Vanno ricaricati tutti.

global gdt_flush

section .text
gdt_flush:
    ; Convenzione cdecl: all'ingresso [esp] = indirizzo di ritorno,
    ; [esp + 4] = primo argomento. Le parentesi quadre significano
    ; "il contenuto della memoria a quell'indirizzo" (come dereferenziare un puntatore).
    mov eax, [esp + 4]
    lgdt [eax]              ; dice alla CPU dove si trova la nuova GDT

    ; Registri di segmento dei dati: tutti sul selettore 0x10 (dati del kernel).
    ; Non si può scrivere `mov ds, 0x10` direttamente: bisogna passare da un registro.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax              ; SS = segmento dello stack

    ; CS (segmento del codice) non si può modificare con `mov`.
    ; L'unico modo è un "salto lontano" (far jump) che indica selettore:indirizzo.
    ; Saltiamo semplicemente all'istruzione successiva, ma con CS = 0x08.
    jmp 0x08:.flush
.flush:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
