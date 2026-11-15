; proc/usermode.asm — Il salto dalla modalità kernel alla modalità utente.
;
; Dal C:   void enter_user_mode(uint32_t entry_point, uint32_t user_stack);
;
; IL PROBLEMA: non esiste un'istruzione "passa in modalità utente". La CPU abbassa
; il livello di privilegio in un solo caso: quando RITORNA da un interrupt (`iret`)
; verso codice meno privilegiato.
;
; IL TRUCCO: fingiamo di tornare da un interrupt che non c'è mai stato. Costruiamo a
; mano sullo stack ciò che la CPU ci avrebbe messo se un interrupt avesse interrotto
; un programma utente, ed eseguiamo `iret`. La CPU "ripristina" quei valori e si
; ritrova in modalità utente, all'indirizzo scelto da noi.
;
;     ciò che `iret` preleva dallo stack, dal basso verso l'alto:
;
;         EIP      <- ESP    dove riprendere: l'inizio del programma
;         CS                 segmento di codice UTENTE: è lui a dire "ring 3"
;         EFLAGS             i flag, con gli interrupt abilitati
;         ESP                lo stack del programma
;         SS                 segmento dello stack utente
;
; È lo stesso trucco di process_create (stack costruito a mano per il `ret` di
; context_switch), un piano più in alto.
;
; COLLEGAMENTO CON LA TEORIA: è il passaggio da modo kernel a modo utente. Il
; passaggio inverso avviene solo tramite interrupt, eccezioni e syscall.

global enter_user_mode

section .text
enter_user_mode:
    mov ecx, [esp + 4]          ; ECX = entry_point
    mov edx, [esp + 8]          ; EDX = user_stack

    ; I registri di segmento dei dati passano al segmento dati utente.
    ; 0x23 = riga 4 della GDT (0x20) + livello di privilegio 3 (GDT_USER_DATA).
    ; SS e CS non si toccano qui: li cambia `iret`.
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Il finto "stato salvato da un interrupt". Ordine dei push inverso a quello
    ; in cui `iret` preleva.
    push dword 0x23             ; SS  = dati utente
    push edx                    ; ESP = stack utente
    push dword 0x202            ; EFLAGS: bit 9 (IF) acceso = interrupt abilitati.
                                ;   Indispensabile: senza, il timer non potrebbe più
                                ;   togliere la CPU al programma. (Il bit 1 è sempre 1.)
    push dword 0x1B             ; CS  = codice utente: 0x18 + privilegio 3 (GDT_USER_CODE)
    push ecx                    ; EIP = prima istruzione del programma

    ; Azzeriamo i registri generali: contengono valori del kernel (indirizzi,
    ; dati) che un programma utente non deve poter vedere.
    xor eax, eax                ; `xor r, r` = modo classico di azzerare un registro
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp

    iret                        ; ...e siamo in modalità utente

section .note.GNU-stack noalloc noexec nowrite progbits
