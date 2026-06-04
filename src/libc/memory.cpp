#include <memory.hpp>
#include <serial.hpp>

static KernelMemoryStatus gMemoryStatus = {};

extern "C" {
extern uint8_t __text_start;
extern uint8_t __text_end;
extern uint8_t __rodata_start;
extern uint8_t __rodata_end;
extern uint8_t __data_start;
extern uint8_t __data_end;
extern uint8_t __bss_start;
extern uint8_t __bss_end;
}

#ifdef __x86_64__

static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t pd[512] __attribute__((aligned(4096)));
static uint64_t pt0[512] __attribute__((aligned(4096)));

static inline uint64_t readCr0() {
    uint64_t value;
    asm volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline void writeCr0(uint64_t value) {
    asm volatile("mov %0, %%cr0" :: "r"(value) : "memory");
}

static inline void writeCr3(uint64_t value) {
    asm volatile("mov %0, %%cr3" :: "r"(value) : "memory");
}

static inline uint64_t readMsr(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static inline void writeMsr(uint32_t msr, uint64_t value) {
    uint32_t lo = static_cast<uint32_t>(value & 0xFFFFFFFFu);
    uint32_t hi = static_cast<uint32_t>(value >> 32);
    asm volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

static bool cpuSupportsNx() {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    eax = 0x80000000u;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001u) {
        return false;
    }
    eax = 0x80000001u;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 20)) != 0;
}

static uintptr_t pageDown(uintptr_t value) {
    return value & ~static_cast<uintptr_t>(0xFFFu);
}

static uintptr_t pageUp(uintptr_t value) {
    return (value + 0xFFFu) & ~static_cast<uintptr_t>(0xFFFu);
}

static bool addressInRange(uintptr_t addr, uintptr_t start, uintptr_t end) {
    return addr >= pageDown(start) && addr < pageUp(end);
}

#endif

void initKernelMemoryProtection() {
    gMemoryStatus.textStart = reinterpret_cast<uintptr_t>(&__text_start);
    gMemoryStatus.textEnd = reinterpret_cast<uintptr_t>(&__text_end);
    gMemoryStatus.rodataStart = reinterpret_cast<uintptr_t>(&__rodata_start);
    gMemoryStatus.rodataEnd = reinterpret_cast<uintptr_t>(&__rodata_end);
    gMemoryStatus.dataStart = reinterpret_cast<uintptr_t>(&__data_start);
    gMemoryStatus.dataEnd = reinterpret_cast<uintptr_t>(&__data_end);
    gMemoryStatus.bssStart = reinterpret_cast<uintptr_t>(&__bss_start);
    gMemoryStatus.bssEnd = reinterpret_cast<uintptr_t>(&__bss_end);

#ifdef __x86_64__
    const uint64_t PTE_PRESENT = 1ull << 0;
    const uint64_t PTE_RW = 1ull << 1;
    const uint64_t PTE_NX = 1ull << 63;

    bool nx = cpuSupportsNx();
    gMemoryStatus.nxSupported = nx;
    if (nx) {
        uint64_t efer = readMsr(0xC0000080u);
        efer |= (1ull << 11); // IA32_EFER.NXE
        writeMsr(0xC0000080u, efer);
        gMemoryStatus.nxEnabled = true;
    }

    for (int i = 0; i < 512; ++i) {
        pml4[i] = 0;
        pdpt[i] = 0;
        pd[i] = 0;
        pt0[i] = 0;
    }

    pml4[0] = reinterpret_cast<uintptr_t>(pdpt) | PTE_PRESENT | PTE_RW;
    pdpt[0] = reinterpret_cast<uintptr_t>(pd) | PTE_PRESENT | PTE_RW;
    pd[0] = reinterpret_cast<uintptr_t>(pt0) | PTE_PRESENT | PTE_RW;

    for (int i = 0; i < 512; ++i) {
        uintptr_t addr = static_cast<uintptr_t>(i) * 4096u;
        bool executable = addressInRange(addr, gMemoryStatus.textStart, gMemoryStatus.textEnd);
        bool readOnly = executable || addressInRange(addr, gMemoryStatus.rodataStart, gMemoryStatus.rodataEnd);
        uint64_t flags = PTE_PRESENT;
        if (!readOnly) {
            flags |= PTE_RW;
        }
        if (nx && !executable) {
            flags |= PTE_NX;
        }
        pt0[i] = static_cast<uint64_t>(addr) | flags;
    }

    uint64_t cr0 = readCr0();
    cr0 |= (1ull << 16); // CR0.WP: supervisor writes obey read-only PTEs.
    writeCr0(cr0);
    gMemoryStatus.writeProtectEnabled = true;

    writeCr3(reinterpret_cast<uintptr_t>(pml4));
    gMemoryStatus.pagingActive = true;
    gMemoryStatus.kernelWxProtected = true;
    Serial::writeString("[memory] x86_64 4K page permissions installed (W^X/NX where supported)\n");
#else
    gMemoryStatus.pagingActive = false;
    gMemoryStatus.nxSupported = false;
    gMemoryStatus.nxEnabled = false;
    gMemoryStatus.writeProtectEnabled = false;
    gMemoryStatus.kernelWxProtected = false;
    Serial::writeString("[memory] 32-bit paging permissions not installed yet\n");
#endif
}

const KernelMemoryStatus& getKernelMemoryStatus() {
    return gMemoryStatus;
}
