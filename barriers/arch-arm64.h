/*
 * arch-arm64.h: AArch64 barrier primitives.
 * Derived from the Linux kernel / perfbook CodeSamples/arch-arm64/arch-arm64.h (GPL v2).
 */
#ifndef ARCH_ARM64_H
#define ARCH_ARM64_H

#define CACHE_LINE_SIZE 128
#define ____cacheline_internodealigned_in_smp \
	__attribute__((__aligned__(CACHE_LINE_SIZE)))

/* Compiler barrier. */
#define barrier() __asm__ __volatile__("" : : : "memory")

/* Full barrier: ARMv8 inner-shareable. */
#define smp_mb() __asm__ __volatile__("dmb ish" : : : "memory")

/* Load barrier / store barrier. */
#define smp_rmb() __asm__ __volatile__("dmb ishld" : : : "memory")
#define smp_wmb() __asm__ __volatile__("dmb ishst" : : : "memory")

/* ARMv8 LDXR/STXR atomics imply no ordering; need explicit barriers. */
#define smp_mb__before_atomic() smp_mb()
#define smp_mb__after_atomic()  smp_mb()

#endif /* ARCH_ARM64_H */
