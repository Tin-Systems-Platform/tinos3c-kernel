global gdt_flush
gdt_flush:
    mov eax, [esp + 4]  ; Haetaan gdt_p osoite pinosta
    lgdt [eax]          ; Ladataan uusi GDT

    ; Päivitetään datasegmentit (0x10 on GDT:n kolmas merkintä)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Päivitetään koodisegmentti (0x08) tekemällä Far Jump
    jmp 0x08:.flush
.flush:
    ret