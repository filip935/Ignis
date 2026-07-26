# Ignis ⚡

A lightweight, native C GUI utility for quickly flashing Windows ISOs onto USB drives on Linux.

## Features
- **Fast & Minimal:** Written in C with a tiny binary (~215KB).
- **Native GUI:** Built natively for Linux desktop environments (GTK4).
- **Open Source:** Released under the MIT License.
  
<img width="705" height="572" alt="Zrzut ekranu_20260726_130043" src="https://github.com/user-attachments/assets/c34edd5a-6959-4094-bf21-ba58e46686e0" />


## Prerequisites

Make sure you have the required dependencies installed on your system:

# Arch Linux / Manjaro
sudo pacman -S gtk4 parted dosfstools ntfs-3g wimlib util-linux grub

# Ubuntu / Debian
sudo apt install libgtk-4-dev parted dosfstools ntfs-3g wimtools util-linux grub-pc-bin grub-efi-amd64-bin
