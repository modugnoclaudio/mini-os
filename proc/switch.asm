; proc/switch.asm — Il CONTEXT SWITCH: il passaggio della CPU da un processo a un altro.
;
; Dal C si chiama così:
;
;     void context_switch(uint32_t *old_esp, uint32_t new_esp);
;
;   old_esp = DOVE salvare lo stack pointer del processo che lascia la CPU
;             (è l'indirizzo del campo `esp` del suo PCB)
;   new_esp = lo stack pointer salvato del processo che la riceve
;
; COLLEGAMENTO CON LA TEORIA: è esattamente il "cambio di contesto" studiato a
; lezione: salvare lo stato del processo uscente, ripristinare quello dell'entrante.
;
; L'IDEA, in una riga: ogni processo ha il suo stack, e lo stato di un processo
; fermo sta tutto in cima al suo stack. Cambiare processo = cambiare ESP.
;
; La cosa sorprendente è che questa funzione viene CHIAMATA da un processo e
; RITORNA in un altro: tra il `call` e il `ret` cambia lo stack, quindi il `ret`
; preleva l'indirizzo di ritorno del processo nuovo, cioè il punto in cui LUI aveva
; chiamato context_switch l'ultima volta. Il processo vecchio resta "congelato"
; dentro questa chiamata finché qualcuno non tornerà a lui.
;
; QUALI REGISTRI SALVARE: solo EBP, EBX, ESI, EDI. La convenzione di chiamata del C
; (cdecl) dice che una funzione può sporcare liberamente EAX, ECX, EDX, ma deve
; lasciare intatti quei quattro. Dal punto di vista del C, context_switch è una
; normale funzione: chi l'ha chiamata ha già messo al sicuro ciò che gli serve.
; Serve l'assembly perché il C non può toccare ESP direttamente.

global context_switch

section .text
context_switch:
    ; Leggiamo gli argomenti PRIMA dei push, finché le loro posizioni sono semplici:
    ; [esp] = indirizzo di ritorno, [esp+4] = primo argomento, [esp+8] = secondo.
    mov eax, [esp + 4]          ; EAX = old_esp (un puntatore)
    mov edx, [esp + 8]          ; EDX = new_esp (un valore)

    ; --- 1. Salviamo il contesto del processo uscente sul SUO stack ---
    push ebp
    push ebx
    push esi
    push edi

    mov [eax], esp              ; *old_esp = ESP   (annotato nel PCB dell'uscente)

    ; --- 2. Il cambio vero e proprio: una sola istruzione ---
    mov esp, edx                ; da qui in poi siamo sullo stack del processo entrante

    ; --- 3. Ripristiniamo il contesto dell'entrante dal SUO stack ---
    ; Sono i valori che lui stesso aveva salvato al punto 1, l'ultima volta che
    ; ha lasciato la CPU (oppure quelli preparati a mano da process_create se
    ; non ha ancora mai girato). L'ordine dei pop è l'inverso dei push.
    pop edi
    pop esi
    pop ebx
    pop ebp

    ret                         ; ritorna... nel processo entrante

section .note.GNU-stack noalloc noexec nowrite progbits
