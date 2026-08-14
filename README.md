# Tinos3c Kernel

This repository contains the main kernel development effort for **Tinos3c**, a new-generation operating system in the TinosC line.

Tinos3c is written in C and is being developed largely from the ground up, with a focus on performance, stability, and feature development. Unlike earlier TinosC projects, Tinos3c is designed around a modular architecture, with the kernel and userspace developed separately.

This repository contains the kernel portion of Tinos3c. The kernel is responsible for the low-level functionality of the operating system, including memory management, hardware interaction, interrupts, and other core functionality.

The project is still under active development and its architecture and features are expected to evolve over time.

## Building

You will need the following tools to compile the kernel:

- Make
- GNU binutils
- Clang
- NASM
- GRUB tooling

The build currently produces a kernel intended to be booted using GRUB.

## Contributing

This repository follows the global Tin Systems Platform contributing guidelines. For more information on how to contribute, see the [Tin Systems Platform Contributing Guidelines](https://github.com/Tin-Systems-Platform/.github/blob/main/CONTRIBUTING.md).

## License

Tinos3c is licensed under the [TINOS License v1.2](LICENSE).