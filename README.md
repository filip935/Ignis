# WinFlash ⚡

A lightweight, native C GUI utility for quickly flashing Windows ISOs onto USB drives on Linux.

## Features
- **Fast & Minimal:** Written in C with a tiny footprint (~215KB).
- **Native GUI:** Built natively for Linux desktop environments (GTK4).
- **Open Source:** Released under the MIT License.
  
<img width="722" height="582" alt="Screenshot_2026-07-25_12-47-59" src="https://github.com/user-attachments/assets/f8ee7c31-1834-4fe2-98ee-f007f91920e1" />

## Prerequisites

Make sure you have the required dependencies installed on your system:

# Arch Linux / Manjaro
sudo pacman -S gtk4 parted dosfstools ntfs-3g wimlib util-linux grub

# Ubuntu / Debian
sudo apt install libgtk-4-dev parted dosfstools ntfs-3g wimtools util-linux grub-pc-bin grub-efi-amd64-bin
