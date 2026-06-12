/*
 * Build-only C example for MrHakOS.
 *
 * This is freestanding-style source: no stdio, no malloc, no host system calls.
 * It compiles to an object file today. It is not runnable inside MrHakOS until
 * the OS has a program loader and syscall/kernel-call ABI.
 */

int mrhakos_c_add(int a, int b) {
    return a + b;
}

int mrhakos_c_hello_status(void) {
    int total = 0;
    for (int i = 1; i <= 3; ++i) {
        total = mrhakos_c_add(total, i);
    }
    return total == 6 ? 0 : 1;
}

