/*
 * barriers.h: arch-independent memory-ordering primitives + arch dispatch.
 * Arch-independent parts mirror perfbook CodeSamples/api.h (GPL v2).
 */
#ifndef BARRIERS_H
#define BARRIERS_H

#if defined(__x86_64__)
#  include "arch-x86.h"
#elif defined(__aarch64__)
#  include "arch-arm64.h"
#elif defined(__arm__)
#  include "arch-arm32.h"
#else
#  error "unsupported architecture: need x86_64, aarch64, or arm (armv7)"
#endif

/* Volatile access: prevent compiler reordering/merging. */
#define ACCESS_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#define READ_ONCE(x)   ({ __typeof__(x) __x = ACCESS_ONCE(x); __x; })
#define WRITE_ONCE(x, v) do { ACCESS_ONCE(x) = (v); } while (0)

/* Release/acquire via gcc atomics (-> stlr/ldar on arm64, plain on x86). */
#define smp_store_release(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define smp_load_acquire(p)     __atomic_load_n((p), __ATOMIC_ACQUIRE)

#endif /* BARRIERS_H */
