/*
 * arch-x86.h: x86_64 barrier primitives.
 * Derived from the Linux kernel / perfbook CodeSamples/arch-x86/arch-x86.h (GPL v2).
 */
#ifndef ARCH_X86_H
#define ARCH_X86_H

#define CACHE_LINE_SIZE 64
#define ____cacheline_internodealigned_in_smp \
	__attribute__((__aligned__(CACHE_LINE_SIZE)))

/* Compiler barrier. */
#define barrier() __asm__ __volatile__("" : : : "memory")

/* Full memory barrier: lock-prefixed add serializes (matches Linux __smp_mb). */
#define smp_mb() __asm__ __volatile__("lock addl $0,-4(%%rsp)" : : : "memory", "cc")

/*
 * x86 is TSO: loads are not reordered with loads, stores not with stores.
 * rmb/wmb only need to constrain the compiler.
 */
#define smp_rmb() barrier()
#define smp_wmb() barrier()

/* Atomics are serializing on x86; before/after barriers are compiler-only. */
#define smp_mb__before_atomic() barrier()
#define smp_mb__after_atomic()  barrier()

#endif /* ARCH_X86_H */
