# MrHakOS Programming Support Roadmap

This document describes what C, C++, and Python support means for MrHakOS today
and what must be implemented before programs can be edited, compiled, and run
inside the OS.

## Current System Model

MrHakOS is currently a single freestanding kernel image. The BIOS or GRUB loads
the kernel, `kernel_main()` initializes hardware-facing subsystems, and control
stays inside the kernel terminal.

Current major components:

- 32-bit and 64-bit boot paths
- Freestanding C++ kernel
- VGA/linear-framebuffer terminal output
- PS/2/USB-legacy keyboard input
- Interrupt and PIT timer setup
- RAM-only hierarchical filesystem
- Built-in kernel shell commands
- PCI and Ethernet networking support
- Minimal libc-style kernel helpers

There is no separate user-space environment yet.

## Answers To The Development Support Questions

| Question | Current answer |
|---|---|
| Kernel only, or user-space too? | Kernel only. There is no user-space process model yet. |
| Shell? | Yes. The terminal is a kernel-resident command interpreter. |
| File system? | Yes, RAM-only. It supports directories and `.hak` text files. |
| Running programs? | No. Shell commands are compiled into the kernel. |
| System calls? | No syscall ABI exists yet. |
| C standard library? | Only minimal freestanding helpers such as string/memory functions. |
| C build system? | Not for user programs yet. Kernel support is mostly C++/Assembly. |
| C++ build system? | Yes for the kernel. No separate user-program C++ runtime exists. |
| Python support possible now? | Not as a real interpreter. Python should be a roadmap item. |
| Safest next step? | Add build-only examples, document constraints, then design a user-space ABI. |

## What C Support Means Today

Today, C support means source can be added to the repository and compiled by the
host build tools as freestanding object code. It does not mean the resulting
object can run inside MrHakOS.

To run a C program inside MrHakOS later, the OS needs at least:

- A program binary format, probably ELF
- A loader that can place program text/data into memory
- A calling convention for entering the program
- Console output syscalls or kernel service calls
- File read/write syscalls
- Memory allocation services
- A process/task structure
- A way to return control to the shell

## What C++ Support Means Today

MrHakOS already uses freestanding C++ for the kernel. That means:

- No hosted C++ standard library
- No exceptions
- No RTTI
- No iostreams
- No dynamic linker
- Kernel `operator new/delete` are custom and limited

Simple C++ code can be compiled into the kernel if it follows those constraints.
Separate C++ user programs require the same user-space features listed for C,
plus a small C++ runtime plan for constructors, destructors, and allocation.

## What Python Support Means Today

Python support is not realistic as an immediate feature. A Python interpreter
needs a much larger OS substrate than MrHakOS currently provides:

- Substantial C standard library support
- Dynamic memory allocation with real free/reuse behavior
- File descriptors or equivalent I/O handles
- Console I/O
- Module loading or embedded frozen modules
- Error handling and stack growth behavior
- A process or interpreter lifecycle
- Enough filesystem support to read scripts and libraries

The right near-term Python step is documentation and source examples only.

## Recommended Roadmap

### Stage 1: Source And Build Preparation

- Keep C/C++ examples outside the kernel image.
- Compile C and C++ examples to freestanding object files.
- Keep Python as syntax-checked source only.
- Avoid claiming that examples run inside MrHakOS.

### Stage 2: Kernel Service Interface

- Define a small syscall or kernel-call ABI.
- Start with console write, exit, and maybe get time.
- Add a stable register convention and error return convention.
- Add shell command documentation for the ABI.

### Stage 3: Executable Loading

- Pick one binary format. ELF32 is the natural first target because the 32-bit
  GRUB path already understands ELF for the kernel.
- Load text, rodata, data, and bss into memory.
- Add a fixed program entrypoint convention.
- Add a shell command such as `run <file>` only after loading is safe.

### Stage 4: Minimal User C Runtime

- Add startup code that calls `main(argc, argv)`.
- Provide tiny wrappers for `write`, `exit`, and later `read`.
- Add simple examples such as `hello`, `args`, and `cat`.

### Stage 5: Memory And Filesystem Growth

- Replace the bump allocator with `kmalloc`/`kfree`.
- Add a physical page allocator.
- Add per-program memory regions.
- Add persistent storage if programs should survive reboot.

### Stage 6: C++ User Programs

- Add constructor handling for `.init_array`.
- Define what C++ features remain disabled.
- Keep exceptions, RTTI, and the hosted standard library disabled until runtime
  support exists.

### Stage 7: Python Feasibility

- Start with a tiny custom scripting language or bytecode interpreter if the goal
  is interactive scripting.
- Consider MicroPython only after the OS has robust allocation, file I/O, and a
  usable C runtime layer.
- Treat full CPython as a long-term port, not a near-term project.

## Build-Only Examples

The repository includes build-only examples under `examples/development/`.
These examples are intentionally not installed into the kernel image and cannot
run in MrHakOS yet.

From the repository root:

```bash
make dev-examples
```

This target:

- Compiles the C example to an object file.
- Compiles the C++ example to an object file.
- Syntax-checks the Python source with the host `python3` if available.

The output goes under `bin/dev-examples/`, matching the existing build artifact
layout.

