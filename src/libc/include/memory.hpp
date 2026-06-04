#ifndef _LIBC_MEMORY_H
#define _LIBC_MEMORY_H

#include <stddef.h>
#include <stdint.h>

struct KernelMemoryStatus {
    bool pagingActive;
    bool nxSupported;
    bool nxEnabled;
    bool writeProtectEnabled;
    bool kernelWxProtected;
    uintptr_t textStart;
    uintptr_t textEnd;
    uintptr_t rodataStart;
    uintptr_t rodataEnd;
    uintptr_t dataStart;
    uintptr_t dataEnd;
    uintptr_t bssStart;
    uintptr_t bssEnd;
};

// Install the first kernel-owned memory protection policy.
// 64-bit: replaces the bootloader's single RWX 2 MiB page with 4 KiB PTEs,
//         enables CR0.WP, and enables NX when the CPU supports it.
// 32-bit: records that protected-mode paging permissions are not installed yet.
void initKernelMemoryProtection();

const KernelMemoryStatus& getKernelMemoryStatus();

#endif // _LIBC_MEMORY_H
