[BITS 32]
extern isr_handler
global isr0
global isr1
global isr_common_stub

%macro ISR_NOERRCODE 1
global isr%1        
isr%1:              
    push byte 0     
    push byte %1    
    jmp isr_common_stub
%endmacro


ISR_NOERRCODE 32
ISR_NOERRCODE 33


isr0: jmp isr32
isr1: jmp isr33

isr_common_stub:
    pusha                  
    mov ax, ds
    push eax                 
    
    mov ax, 0x10             
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call isr_handler         

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa                     
    add esp, 8            
    iret                     

section .text
global idt_flush
idt_flush:
    mov eax, [esp+4]  
    lidt [eax]        
    ret