ENTRY(_start)

SECTIONS
{
    . = 1M;

    kernel_start = .;


    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.text.prologue)
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    kernel_end = .;

    end = ALIGN(4K);

    /DISCARD/ :
	{
		*(.comment)
		*(.eh_frame)
		*(.note.gnu.build-id)
	}
}