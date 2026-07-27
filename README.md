# NihilFlash

A lightweight, native C utility for flashing ISOs onto USB drives on Linux.

## Features
- **Windows ISOs:** Full support with MBR/GPT partition tables, FAT32/NTFS, UEFI/Legacy boot, WIM splitting
- **Linux ISOs:** Raw dd writing for Ubuntu, Fedora, Arch, and more
- **BSD ISOs:** Raw dd writing for FreeBSD, OpenBSD, NetBSD
- **Fast & Minimal:** Written in C with a tiny binary
- **Native GUI:** Built natively for Linux desktop environments (GTK4)
- **Open Source:** Released under the MIT License.

## Prerequisites

Make sure you have the required dependencies installed on your system:

# Arch Linux / Manjaro
sudo pacman -S gtk4 parted dosfstools ntfs-3g wimlib util-linux grub

# Ubuntu / Debian
sudo apt install libgtk-4-dev parted dosfstools ntfs-3g wimtools util-linux grub-pc-bin grub-efi-amd64-bin
