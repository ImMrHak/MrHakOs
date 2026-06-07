#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO_PATH="$PROJECT_ROOT/bin/mrhakos-grub.iso"
MAKE_TARGET="iso"
YES=0
DRY_RUN=0
NO_BUILD=0
VERIFY_WRITE=1
EJECT=1
FORCE_NON_USB=0
BLOCK_SIZE="4M"
DEVICE=""

usage() {
  cat <<'EOF'
Usage:
  scripts/build_iso_usb.sh --device /dev/sdX
  scripts/build_iso_usb.sh --device /dev/sdX --yes
  scripts/build_iso_usb.sh --device /dev/sdX --dry-run
  scripts/build_iso_usb.sh --device /dev/sdX --no-build --iso bin/mrhakos-grub.iso
  scripts/build_iso_usb.sh --list

What it does:
  1. Builds and verifies the GRUB bootable MrHakOS ISO with: make iso
  2. Safely checks that the target is a whole disk, not a partition
  3. Writes bin/mrhakos-grub.iso to the USB block device with dd
  4. Verifies the bytes written by comparing the device against the ISO
  5. Runs sync/eject when done

Options:
  -d, --device DEV       Whole target disk, for example /dev/sdb or /dev/nvme0n1
  -y, --yes             Skip the interactive WRITE confirmation
      --dry-run         Show what would happen without writing
      --list            Show removable/USB-looking disks and exit
      --iso PATH        ISO path to write, default: bin/mrhakos-grub.iso
      --make-target T   Make target to build before writing, default: iso
      --no-build        Do not run make; use the existing ISO file
      --no-verify       Skip post-write byte comparison
      --no-eject        Do not eject the target after sync
      --force-non-usb   Allow writing a non-USB/non-removable disk
      --block-size BS   dd block size, default: 4M
  -h, --help            Show this help

WARNING:
  This DESTROYS all data on the target device.
  Pass the whole disk, for example /dev/sdb or /dev/nvme0n1,
  NOT a partition like /dev/sdb1 or /dev/nvme0n1p1.

Helpful commands:
  lsblk -o NAME,SIZE,MODEL,TRAN,RM,TYPE,MOUNTPOINTS
  sudo dmesg -w
EOF
}

log() { printf '=> %s\n' "$*"; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

sudo_cmd() {
  if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

show_disks() {
  log "Available disks"
  lsblk -d -o NAME,SIZE,MODEL,TRAN,RM,TYPE,MOUNTPOINTS
  printf '\nTip: choose the whole USB disk path, not a partition.\n'
}

canonical_device() {
  local dev="$1"
  readlink -f "$dev"
}

root_parent_disk() {
  local root_src root_pk
  root_src="$(findmnt -n -o SOURCE / 2>/dev/null || true)"
  [[ -n "$root_src" ]] || return 0
  root_pk="$(lsblk -no PKNAME "$root_src" 2>/dev/null | head -n1 || true)"
  if [[ -n "$root_pk" ]]; then
    readlink -f "/dev/$root_pk"
  else
    readlink -f "$root_src" 2>/dev/null || true
  fi
}

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
    --list)
      have lsblk || fail "lsblk is not installed"
      show_disks
      exit 0
      ;;
    --iso)
      [[ $# -ge 2 ]] || fail "--iso requires a value"
      ISO_PATH="$2"
      shift 2
      ;;
    --make-target)
      [[ $# -ge 2 ]] || fail "--make-target requires a value"
      MAKE_TARGET="$2"
      shift 2
      ;;
    --no-build)
      NO_BUILD=1
      shift
      ;;
    --no-verify)
      VERIFY_WRITE=0
      shift
      ;;
    --no-eject)
      EJECT=0
      shift
      ;;
    --force-non-usb)
      FORCE_NON_USB=1
      shift
      ;;
    --block-size)
      [[ $# -ge 2 ]] || fail "--block-size requires a value"
      BLOCK_SIZE="$2"
      shift 2
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

for tool in lsblk findmnt readlink awk stat sha256sum dd sync cmp blockdev; do
  have "$tool" || fail "$tool is not installed"
done
[[ "$NO_BUILD" -eq 1 ]] || have make || fail "make is not installed"
if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  have sudo || fail "sudo is not installed; run as root or install sudo"
fi

[[ -n "$DEVICE" ]] || { usage; fail "missing --device /dev/sdX"; }
[[ "$DEVICE" == /dev/* ]] || fail "device must be an absolute /dev path"
[[ -b "$DEVICE" ]] || fail "$DEVICE is not a block device"

DEVICE="$(canonical_device "$DEVICE")"
[[ -b "$DEVICE" ]] || fail "resolved device is not a block device: $DEVICE"

device_type="$(lsblk -dn -o TYPE "$DEVICE" | tr -d '[:space:]')"
[[ "$device_type" == "disk" ]] || fail "$DEVICE is type '$device_type'. Use the whole disk, not a partition"

root_disk="$(root_parent_disk)"
if [[ -n "$root_disk" && "$DEVICE" == "$root_disk" ]]; then
  fail "refusing to overwrite the disk that contains '/': $DEVICE"
fi

tran="$(lsblk -dn -o TRAN "$DEVICE" | tr -d '[:space:]' || true)"
rm_flag="$(lsblk -dn -o RM "$DEVICE" | tr -d '[:space:]' || true)"
if [[ "$FORCE_NON_USB" -ne 1 && "$tran" != "usb" && "$rm_flag" != "1" ]]; then
  show_disks >&2 || true
  fail "$DEVICE does not look USB/removable (TRAN='${tran:-unknown}', RM='${rm_flag:-unknown}'). Pass --force-non-usb only if you are absolutely sure"
fi

if [[ "$NO_BUILD" -eq 0 ]]; then
  log "Project: $PROJECT_ROOT"
  log "Building ISO: make $MAKE_TARGET"
  make -C "$PROJECT_ROOT" "$MAKE_TARGET"
else
  log "Skipping build because --no-build was passed"
fi

[[ -s "$ISO_PATH" ]] || fail "ISO was not found or is empty: $ISO_PATH"
ISO_PATH="$(readlink -f "$ISO_PATH")"
iso_size="$(stat -c '%s' "$ISO_PATH")"
iso_sha="$(sha256sum "$ISO_PATH" | awk '{print $1}')"

log "ISO ready: $ISO_PATH"
log "ISO size: $iso_size bytes"
log "ISO SHA256: $iso_sha"

log "Target device summary"
lsblk -o NAME,SIZE,MODEL,TRAN,RM,TYPE,MOUNTPOINTS "$DEVICE"

device_size="$(blockdev --getsize64 "$DEVICE" 2>/dev/null || true)"
if [[ -n "$device_size" && "$device_size" -lt "$iso_size" ]]; then
  fail "$DEVICE is too small: device=$device_size bytes, iso=$iso_size bytes"
fi

if lsblk -nr -o MOUNTPOINTS "$DEVICE" | grep -q .; then
  fail "$DEVICE or one of its partitions is mounted. Unmount it first, e.g. sudo umount ${DEVICE}?*"
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "Dry run: would write ISO with: dd if=$ISO_PATH of=$DEVICE bs=$BLOCK_SIZE status=progress conv=fsync"
  [[ "$VERIFY_WRITE" -eq 1 ]] && log "Dry run: would verify with: cmp -n $iso_size $ISO_PATH $DEVICE"
  exit 0
fi

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  log "Refreshing sudo credentials"
  sudo -v
fi

if [[ "$YES" -ne 1 ]]; then
  printf '\nThis will ERASE ALL DATA on %s.\n' "$DEVICE"
  printf 'Target details:\n'
  lsblk -dno NAME,SIZE,MODEL,TRAN,RM,TYPE "$DEVICE"
  printf 'Type exactly WRITE to continue: '
  read -r answer
  [[ "$answer" == "WRITE" ]] || fail "aborted"
fi

log "Writing ISO to $DEVICE"
sudo_cmd dd if="$ISO_PATH" of="$DEVICE" bs="$BLOCK_SIZE" status=progress conv=fsync
sudo_cmd sync

if [[ "$VERIFY_WRITE" -eq 1 ]]; then
  log "Verifying written bytes"
  sudo_cmd cmp -n "$iso_size" "$ISO_PATH" "$DEVICE"
  log "Write verification passed"
fi

if [[ "$EJECT" -eq 1 ]]; then
  log "Ejecting $DEVICE"
  sudo_cmd eject "$DEVICE" || log "eject failed or unsupported; it is still safe after sync completes"
fi

log "Done. USB is ready to boot MrHakOS."
