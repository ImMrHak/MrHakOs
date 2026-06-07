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
    // Long mode is entered by the GRUB Multiboot2 bootstrap, which installs a
    // broad identity map (currently first 16 GiB) before jumping here. Keep that
    // map active for now so UEFI GOP framebuffers on modern GPUs remain
    // addressable. The previous x86_64 code replaced CR3 with a tiny 2 MiB map,
    // which could immediately page-fault when the framebuffer lived above low
    // memory. A later hardening pass can rebuild page permissions after parsing
    // the Multiboot memory map and framebuffer address.
    gMemoryStatus.pagingActive = true;
    gMemoryStatus.nxSupported = false;
    gMemoryStatus.nxEnabled = false;
    gMemoryStatus.writeProtectEnabled = false;
    gMemoryStatus.kernelWxProtected = false;
    Serial::writeString("[memory] x86_64 long-mode identity map kept for UEFI framebuffer compatibility\n");
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
