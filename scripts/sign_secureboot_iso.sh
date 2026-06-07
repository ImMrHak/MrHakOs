#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/bin"
INPUT_ISO="${1:-$BUILD_DIR/mrhakos-grub.iso}"
OUTPUT_EFI_IMG="${2:-$BUILD_DIR/secureboot/efi-signed.img}"
OUTPUT_ISO="${3:-$BUILD_DIR/mrhakos-grub-secureboot.iso}"
WORK_DIR="$BUILD_DIR/secureboot"
KEY="$WORK_DIR/MrHakOS-SecureBoot.key"
CRT="$WORK_DIR/MrHakOS-SecureBoot.crt"
DER="$WORK_DIR/MrHakOS-SecureBoot.der"
EFI_IMG="$WORK_DIR/efi.img"
SIGNED_DIR="$WORK_DIR/signed"
# shimx64.efi.signed is Microsoft-signed; mmx64.efi.signed is Debian-signed and
# is the only MokManager that shim itself will launch under Secure Boot (shim
# verifies MokManager against its embedded Debian vendor cert). The unsigned
# mmx64.efi fails that check, so MOK enrollment would be impossible on real HW.
SHIM_X64="/usr/lib/shim/shimx64.efi.signed"
MOK_MANAGER_X64="/usr/lib/shim/mmx64.efi.signed"
REPORT="$WORK_DIR/README-secureboot.txt"

log() { printf '=> %s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[[ -s "$INPUT_ISO" ]] || fail "input ISO not found: $INPUT_ISO"
for tool in xorriso mdir mcopy openssl sbsign sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing tool: $tool"
done

mkdir -p "$WORK_DIR" "$SIGNED_DIR" "$(dirname "$OUTPUT_EFI_IMG")"

if [[ ! -s "$KEY" || ! -s "$CRT" || ! -s "$DER" ]]; then
  log "Generating self-signed MrHakOS Secure Boot/MOK key"
  openssl req -new -x509 -newkey rsa:2048 \
    -keyout "$KEY" -out "$CRT" -nodes -days 3650 \
    -subj "/CN=MrHakOS Secure Boot/" >/dev/null 2>&1
  openssl x509 -in "$CRT" -outform DER -out "$DER"
else
  log "Reusing existing Secure Boot key: $CRT"
fi
chmod 600 "$KEY"

rm -f "$EFI_IMG" "$OUTPUT_EFI_IMG"
log "Extracting EFI boot image from ISO"
xorriso -osirrox on -indev "$INPUT_ISO" -extract /efi.img "$EFI_IMG" >/dev/null 2>&1
chmod u+rw "$EFI_IMG"

boot_entries=()
if mdir -i "$EFI_IMG" ::/EFI/BOOT/BOOTX64.EFI >/dev/null 2>&1; then
  boot_entries+=(BOOTX64.EFI)
fi
if mdir -i "$EFI_IMG" ::/EFI/BOOT/BOOTIA32.EFI >/dev/null 2>&1; then
  boot_entries+=(BOOTIA32.EFI)
fi
[[ ${#boot_entries[@]} -gt 0 ]] || fail "no EFI fallback bootloaders found in /EFI/BOOT"

for efi in "${boot_entries[@]}"; do
  src="$WORK_DIR/$efi"
  signed="$SIGNED_DIR/$efi"
  rm -f "$src" "$signed"
  mcopy -o -i "$EFI_IMG" "::/EFI/BOOT/$efi" "$src"
  log "Signing $efi"
  sbsign --key "$KEY" --cert "$CRT" --output "$signed" "$src" >/dev/null
  mcopy -o -i "$EFI_IMG" "$signed" "::/EFI/BOOT/$efi"
done

if [[ -s "$SHIM_X64" && -s "$MOK_MANAGER_X64" && -s "$SIGNED_DIR/BOOTX64.EFI" ]]; then
  log "Installing Microsoft-signed shim fallback for Secure Boot x64"
  # With Secure Boot enabled, firmware trusts shimx64.efi.signed. Shim then loads
  # grubx64.efi from the same directory. grubx64.efi is signed with the MrHakOS
  # MOK key above, so the user only has to enroll MrHakOS-SecureBoot.der once.
  mcopy -o -i "$EFI_IMG" "$SIGNED_DIR/BOOTX64.EFI" "::/EFI/BOOT/grubx64.efi"
  mcopy -o -i "$EFI_IMG" "$SHIM_X64" "::/EFI/BOOT/BOOTX64.EFI"
  mcopy -o -i "$EFI_IMG" "$MOK_MANAGER_X64" "::/EFI/BOOT/mmx64.efi"
else
  log "shim is unavailable; x64 BOOTX64.EFI remains self-signed and requires direct firmware trust/MOK support"
fi

cp "$EFI_IMG" "$OUTPUT_EFI_IMG"
sha256sum "$OUTPUT_EFI_IMG" | tee "$OUTPUT_EFI_IMG.sha256"

log "Building Secure Boot ISO with signed EFI boot image"
# We deliberately do NOT use `xorriso ... -boot_image any replay` to swap the
# image: replay looks the EFI El Torito entry up by its original block address,
# which disappears once the file is remapped, so the UEFI boot entry gets
# dropped ("Cannot enable EL Torito boot image #2"). Instead we overwrite the
# embedded /efi.img in place. mcopy only rewrites files *inside* the FAT image,
# so the signed image is byte-for-byte the same size as the embedded one; every
# boot record (BIOS El Torito, UEFI El Torito, GPT, APM) keeps pointing at the
# same extent and stays valid, while the signed EFI binaries take effect.
rm -f "$OUTPUT_ISO"
lba_line="$(xorriso -indev "$INPUT_ISO" -find /efi.img -exec report_lba -- 2>/dev/null | grep -i '/efi.img')"
[[ -n "$lba_line" ]] || fail "could not locate /efi.img inside $INPUT_ISO"
# Format: "File data lba:  <ext> , <start_lba> , <blocks> , <size> , '/efi.img'"
efi_lba="$(awk -F',' '{gsub(/ /,"",$2); print $2}' <<<"$lba_line")"
efi_size="$(awk -F',' '{gsub(/ /,"",$4); print $4}' <<<"$lba_line")"
signed_size="$(stat -c%s "$OUTPUT_EFI_IMG")"
[[ "$efi_lba" =~ ^[0-9]+$ && "$efi_size" =~ ^[0-9]+$ ]] || fail "unexpected report_lba output: $lba_line"
[[ "$signed_size" == "$efi_size" ]] || fail "signed efi.img ($signed_size B) != embedded efi.img ($efi_size B); in-place overwrite unsafe"
cp "$INPUT_ISO" "$OUTPUT_ISO"
# ISO 2048-byte logical blocks -> byte offset = lba * 2048.
dd if="$OUTPUT_EFI_IMG" of="$OUTPUT_ISO" bs=2048 seek="$efi_lba" conv=notrunc status=none
sha256sum "$OUTPUT_ISO" | tee "$OUTPUT_ISO.sha256"

cat > "$REPORT" <<EOF
MrHakOS Secure Boot notes
=========================

Signed EFI boot image:
  $OUTPUT_EFI_IMG

Certificate to enroll in firmware/MOK:
  $DER

Signed fallback EFI bootloaders also saved under:
  $SIGNED_DIR

Signed fallback EFI bootloaders:
  ${boot_entries[*]}

Important:
  For x64 Secure Boot this script uses a Microsoft-signed shim when available:
    EFI/BOOT/BOOTX64.EFI = shimx64.efi.signed
    EFI/BOOT/grubx64.efi = MrHakOS-signed GRUB fallback loader
    EFI/BOOT/mmx64.efi   = MokManager

  Secure Boot cannot be made universal by code alone: firmware must trust the
  next-stage bootloader. On normal PCs, shim is already trusted by Microsoft UEFI
  CA, but GRUB still needs the MrHakOS certificate enrolled through MOK once.

Typical MOK enrollment flow:
  sudo mokutil --import "$DER"
  reboot
  follow the blue MOK enrollment screen
  then boot again with Secure Boot enabled

Alternative firmware-owner flow:
  Enroll $DER directly into the firmware db if your BIOS/UEFI setup supports
  custom Secure Boot keys.

How to use the signed files manually:
  1. Create/use a FAT32 EFI System Partition on a USB drive.
  2. Copy the signed EFI image contents to that partition.
  3. For x64 Secure Boot, keep BOOTX64.EFI, grubx64.efi, and mmx64.efi together
     under EFI/BOOT.
  4. Keep /boot/mrhakos-longmode.elf, /boot/mrhakos-kernel64.bin,
     /boot/mrhakos-kernel.elf, and /boot/grub/grub.cfg from the normal ISO layout.

If the PC is locked down and does not allow enrolling your own key/MOK, no
self-built OS image can boot with Secure Boot enabled unless its bootloader is
signed by a key already trusted by that exact firmware.
EOF

log "Wrote signed EFI image: $OUTPUT_EFI_IMG"
log "Wrote Secure Boot notes: $REPORT"
