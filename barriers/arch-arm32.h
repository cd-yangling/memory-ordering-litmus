/*
 * arch-arm32.h: ARMv7-A (32-bit) barrier primitives.
 * Derived from the Linux kernel arch/arm/include/asm/barrier.h (GPL v2).
 */
#ifndef ARCH_ARM32_H
#define ARCH_ARM32_H

/* Safe for Cortex-A9 (32B) and A7/A53 (64B) lines: aligning to 64 separates
 * variables even when the real line is 32. */
#define CACHE_LINE_SIZE 64
#define ____cacheline_internodealigned_in_smp \
	__attribute__((__aligned__(CACHE_LINE_SIZE)))

/* Compiler barrier. */
#define barrier() __asm__ __volatile__("" : : : "memory")

/*
 * ARMv7-A/ARMv8-A inner-shareable barriers. NOTE: 'dmb ishld' is ARMv8-only.
 */
#define smp_mb()  __asm__ __volatile__("dmb ish" : : : "memory")
#if defined(__ARM_ARCH) && __ARM_ARCH >= 8
#define smp_rmb() __asm__ __volatile__("dmb ishld" : : : "memory")
#else
#define smp_rmb() __asm__ __volatile__("dmb ish" : : : "memory")
#endif
#define smp_wmb() __asm__ __volatile__("dmb ishst" : : : "memory")

/* ARMv7 ldrex/strex atomics imply no ordering; need explicit barriers. */
#define smp_mb__before_atomic() smp_mb()
#define smp_mb__after_atomic()  smp_mb()

#endif /* ARCH_ARM32_H */
