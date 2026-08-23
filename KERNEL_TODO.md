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

## Stability
- [ ] Fix bugs when having bugs
- [ ] Test kernel stability
- [ ] Handle kernel panics gracefully

## Internal
- [ ] Remove the "debug" console from the kernel