#!/usr/bin/env python3
"""Emit QEMU HMP sendkey commands for simple US-layout text input."""
import sys
import time

KEYS = {
    ' ': 'spc',
    '\n': 'ret',
    '\r': 'ret',
    '.': 'dot',
    '/': 'slash',
    '-': 'minus',
    '_': 'shift-minus',
    '>': 'shift-dot',
    '<': 'shift-comma',
}

def key_for(ch: str) -> str:
    if 'a' <= ch <= 'z' or '0' <= ch <= '9':
        return ch
    if 'A' <= ch <= 'Z':
        return 'shift-' + ch.lower()
    if ch in KEYS:
        return KEYS[ch]
    raise SystemExit(f"Unsupported character for QEMU sendkey: {ch!r}")

def main() -> None:
    delay = float(sys.argv[1]) if len(sys.argv) > 1 else 0.04
    payload = sys.stdin.read()
    for ch in payload:
        print(f"sendkey {key_for(ch)}", flush=True)
        if delay > 0:
            time.sleep(delay)

if __name__ == '__main__':
    main()
