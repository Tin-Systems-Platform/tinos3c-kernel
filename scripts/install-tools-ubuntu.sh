#!/bin/bash
set -e

# Update package lists and install dependencies
sudo apt update
sudo apt install -y make binutils clang nasm xorriso qemu-system-i386 grub-pc-bin mtools
echo "All dependencies have been installed successfully."