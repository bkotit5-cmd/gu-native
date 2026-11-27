// Android AArch64 executable arena for the interpreted-code JIT.
// If Android denies RWX mappings, jit_mem_alloc returns NULL and the runtime keeps using the interpreter.
#if defined(__ANDROID__) && defined(__aarch64__) && !defined(MHGU_NO_JIT)
#include <stddef.h>
#include <sys/mman.h>

void *jit_mem_alloc(size_t n) {
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

int jit_mem_register(void *base, void *rf) {
    (void)base;
    (void)rf;
    return 1;
}

void jit_mem_free(void *base, size_t n, void *rf) {
    (void)rf;
    if (base) munmap(base, n);
}
#endif
