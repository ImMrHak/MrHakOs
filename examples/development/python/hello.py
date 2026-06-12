"""Python roadmap source for MrHakOS.

This file is intentionally source-only today. The repository can syntax-check it
with host Python, but MrHakOS cannot execute Python scripts until it has a
runtime substrate such as file I/O, memory allocation, syscalls, and either a
small interpreter or a port such as MicroPython.
"""


def main() -> int:
    total = sum((1, 2, 3))
    return 0 if total == 6 else 1


if __name__ == "__main__":
    raise SystemExit(main())

