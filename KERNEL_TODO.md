# Tinos3c kernel TODO

## C Library
- [ ] Custom C library implementation (at least C99 compatibility)
- [ ] Possible C17 support

## Memory Management
- [X] Physical Memory Manager (PMM)
- [X] Virtual Memory Manager (VMM)
- [ ] Kernel heap
- [ ] `kmalloc` / `kfree`
- [ ] Memory protection

## Drivers
- [ ] Basic framebuffer
- [ ] Mouse drivers
- [ ] Network drivers
- [ ] Audio drivers
- [ ] Disk drivers
- [ ] PCIe drivers
- [ ] AHCI Drivers

## Stability
- [ ] Fix bugs when having bugs
- [ ] Test kernel stability
- [ ] Handle kernel panics gracefully

## Utility
- [X] Power management
- [X] ACPI integration
- [ ] Scheduler
- [ ] Filesystem stuff


## Internal
- [ ] Remove the "debug" console from the kernel
- [X] Port kernel to x86_64 long mode


## Userspace
- [ ] Custom Userspace
- [ ] Interracts with kernel features

## Custom Scripting language for userspace

Custom Programming language
- [ ] Interpeted language
- [ ] Stdlib working with the kernel stdlib
- [ ] Allow kernel interaction for some parts and/or functions
