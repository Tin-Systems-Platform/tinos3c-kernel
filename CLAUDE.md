# Tinosc Kernel

This is the kernel of the Tinosc line of operating systems developed by Randomusert on GitHub.

You are an expert in low-level systems programming, C, and Assembly. Code should be written with kernel development constraints in mind. Do not assume that normal userspace or POSIX functionality is available.

## Project Structure

Most kernel source code is located in the `kernel/` directory.

- `kernel/console`: Temporary debug console used during development. Do not modify it unless explicitly requested.
- `kernel/drivers`: Hardware driver implementations.
- `kernel/gdt`: Global Descriptor Table (GDT) related code.
- `kernel/idt`: Interrupt Descriptor Table (IDT) related code.
- `kernel/lib`: Internal kernel library code and the custom C standard library implementation.
- `kernel/mm`: Physical and virtual memory management.
- `kernel/pic`: Programmable Interrupt Controller (PIC) related code.
- `kernel/fs`: Virtual Filesystem (VFS) Implementation

## Kernel Development Rules

- This is freestanding kernel code, not normal userspace C.
- Do not assume POSIX or hosted C library functionality is available.
- Prefer existing implementations in `kernel/lib` over relying on an external libc.
- Avoid introducing dependencies on userspace functionality.
- Keep hardware-specific code isolated in the appropriate subsystem.
- Do not modify temporary/debug components unless necessary or explicitly requested.
- Preserve existing interfaces unless there is a good reason to change them.
- If a function is intended for reuse across multiple subsystems, implement it in kernel/lib. Reuse an existing `kernel/lib` implementation when one is available instead of creating a duplicate. Functions specific to a subsystem should remain internal to that subsystem.
- Everything memory related MUST be safe and validate pointers if those places need doing so.

# Documentation
All new functions, public APIs, and significant internal functions must include Doxygen documentation when introduced.

Documentation should describe the function's purpose, parameters, return value, and relevant side effects/errors where applicable.

Also add a @copyright notice for the LICENSE.md in the root of the folder as well as a @date for giving the date of implementation for ALL new functions.