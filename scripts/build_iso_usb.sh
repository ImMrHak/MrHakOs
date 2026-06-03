#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO_PATH="$PROJECT_ROOT/bin/mrhakos-grub.iso"
MAKE_TARGET="grubiso"
YES=0
DRY_RUN=0
DEVICE=""

usage() {
  cat <<'EOF'
Usage:
  scripts/build_iso_usb.sh --device /dev/sdX
  scripts/build_iso_usb.sh --device /dev/sdX --yes
  scripts/build_iso_usb.sh --device /dev/sdX --dry-run

What it does:
  1. Builds the GRUB bootable MrHakOS ISO with: make grubiso
  2. Writes bin/mrhakos-grub.iso to the USB block device with dd
  3. Runs sync/eject when done

WARNING:
  This DESTROYS all data on the target device.
  Pass the whole USB disk, for example /dev/sdb, NOT a partition like /dev/sdb1.

Helpful commands:
  lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
  sudo dmesg -w
EOF
}

log() { printf '=> %s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device|-d)
      [[ $# -ge 2 ]] || fail "--device requires a value"
      DEVICE="$2"
      shift 2
      ;;
    --yes|-y)
      YES=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      fail "unknown argument: $1"
      ;;
  esac
done

[[ -n "$DEVICE" ]] || { usage; fail "missing --device /dev/sdX"; }
[[ "$DEVICE" == /dev/* ]] || fail "device must be an absolute /dev path"
[[ -b "$DEVICE" ]] || fail "$DEVICE is not a block device"
[[ ! "$DEVICE" =~ [0-9]$ ]] || fail "$DEVICE looks like a partition. Use the whole disk, e.g. /dev/sdb not /dev/sdb1"
[[ "$DEVICE" != "/dev/sda" ]] || fail "refusing to write /dev/sda by default; verify your USB device with lsblk"

command -v make >/dev/null || fail "make is not installed"
command -v dd >/dev/null || fail "dd is not installed"
command -v lsblk >/dev/null || fail "lsblk is not installed"
command -v sudo >/dev/null || fail "sudo is not installed"

log "Project: $PROJECT_ROOT"
log "Building ISO: make $MAKE_TARGET"
make -C "$PROJECT_ROOT" "$MAKE_TARGET"
[[ -s "$ISO_PATH" ]] || fail "ISO was not created at $ISO_PATH"

log "ISO ready: $ISO_PATH"
ls -lh "$ISO_PATH"

log "Target device summary"
lsblk -o NAME,SIZE,MODEL,TRAN,TYPE,MOUNTPOINTS "$DEVICE"

if lsblk -nr -o MOUNTPOINTS "$DEVICE" | grep -q .; then
  fail "$DEVICE or one of its partitions is mounted. Unmount it first, e.g. sudo umount ${DEVICE}?*"
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "Dry run: would write ISO with: sudo dd if=$ISO_PATH of=$DEVICE bs=4M status=progress conv=fsync"
  exit 0
fi

if [[ "$YES" -ne 1 ]]; then
  printf '\nThis will ERASE ALL DATA on %s.\n' "$DEVICE"
  printf 'Type exactly WRITE to continue: '
  read -r answer
  [[ "$answer" == "WRITE" ]] || fail "aborted"
fi

log "Writing ISO to $DEVICE"
sudo dd if="$ISO_PATH" of="$DEVICE" bs=4M status=progress conv=fsync
sync

log "Ejecting $DEVICE"
sudo eject "$DEVICE" || log "eject failed or unsupported; it is still safe after sync completes"

log "Done. USB is ready to boot MrHakOS."
