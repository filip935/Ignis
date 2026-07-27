#!/bin/bash
set -e

OUTDIR="$1"
if [ -z "$OUTDIR" ]; then
    OUTDIR="."
fi

HEADER="${OUTDIR}/bootdata.h"

echo "Generating boot data..."
echo "  Output: ${HEADER}"

echo "#ifndef NIHILFLASH_BOOTDATA_H" > "${HEADER}"
echo "#define NIHILFLASH_BOOTDATA_H" >> "${HEADER}"
echo "" >> "${HEADER}"

# 1. Read GRUB boot.img (512 bytes MBR stage1)
BOOT_IMG="/usr/lib/grub/i386-pc/boot.img"
if [ -f "${BOOT_IMG}" ]; then
    SIZE=$(stat --format="%s" "${BOOT_IMG}")
    echo "/* GRUB boot.img (${SIZE} bytes) - MBR stage1 */" >> "${HEADER}"
    echo "static const unsigned char grub_boot_img[${SIZE}] = {" >> "${HEADER}"
    hexdump -v -e '1/1 "0x%02x,"' "${BOOT_IMG}" >> "${HEADER}"
    echo "" >> "${HEADER}"
    echo "};" >> "${HEADER}"
    echo "static const unsigned int grub_boot_img_size = ${SIZE};" >> "${HEADER}"
    echo "  GRUB boot.img: ${SIZE} bytes"
else
    echo "static const unsigned char grub_boot_img[1] = {0};" >> "${HEADER}"
    echo "static const unsigned int grub_boot_img_size = 0;" >> "${HEADER}"
    echo "  WARNING: GRUB boot.img not found"
fi
echo "" >> "${HEADER}"

# 2. Generate GRUB core.img with needed modules
CORE_IMG=$(mktemp /tmp/grub-core-XXXXXX.img)
echo "  Generating core.img..."
if grub-mkimage -O i386-pc -o "${CORE_IMG}" \
    --prefix='(hd0,msdos1)/boot/grub' \
    fat part_msdos chain ntldr search configfile biosdisk \
    2>/dev/null; then
    SIZE=$(stat --format="%s" "${CORE_IMG}")
    echo "/* GRUB core.img (${SIZE} bytes) - stage2 */" >> "${HEADER}"
    echo "static const unsigned char grub_core_img[${SIZE}] = {" >> "${HEADER}"
    hexdump -v -e '1/1 "0x%02x,"' "${CORE_IMG}" >> "${HEADER}"
    echo "" >> "${HEADER}"
    echo "};" >> "${HEADER}"
    echo "static const unsigned int grub_core_img_size = ${SIZE};" >> "${HEADER}"
    echo "  GRUB core.img: ${SIZE} bytes"
else
    echo "static const unsigned char grub_core_img[1] = {0};" >> "${HEADER}"
    echo "static const unsigned int grub_core_img_size = 0;" >> "${HEADER}"
    echo "  WARNING: grub-mkimage failed"
fi
rm -f "${CORE_IMG}"

echo "" >> "${HEADER}"

# 3. Embed UEFI:NTFS bootx64_signed.efi
NTFS_EFI="/tmp/bootx64_signed.efi"
if [ ! -f "${NTFS_EFI}" ]; then
    NTFS_EFI="$(dirname "$0")/../bootx64_signed.efi"
fi
if [ -f "${NTFS_EFI}" ]; then
    SIZE=$(stat --format="%s" "${NTFS_EFI}")
    echo "/* UEFI:NTFS bootx64_signed.efi (${SIZE} bytes) */" >> "${HEADER}"
    echo "static const unsigned char uefi_ntfs_efi[${SIZE}] = {" >> "${HEADER}"
    hexdump -v -e '1/1 "0x%02x,"' "${NTFS_EFI}" >> "${HEADER}"
    echo "" >> "${HEADER}"
    echo "};" >> "${HEADER}"
    echo "static const unsigned int uefi_ntfs_efi_size = ${SIZE};" >> "${HEADER}"
    echo "  UEFI:NTFS EFI: ${SIZE} bytes"
else
    echo "static const unsigned char uefi_ntfs_efi[1] = {0};" >> "${HEADER}"
    echo "static const unsigned int uefi_ntfs_efi_size = 0;" >> "${HEADER}"
    echo "  WARNING: UEFI:NTFS EFI not found"
fi

echo "" >> "${HEADER}"
echo "#endif /* NIHILFLASH_BOOTDATA_H */" >> "${HEADER}"
echo "Done."
