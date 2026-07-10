# Makefile for the memory-ordering litmus tests (TSO/SB, RMO/MP, RMO/SPSC).
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra
LDLIBS  ?= -lpthread
N       ?= 10000000

ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
  ARCHTAG := x86_64
else ifeq ($(ARCH),aarch64)
  ARCHTAG := aarch64
else
  $(error unsupported arch: $(ARCH) (need x86_64 or aarch64))
endif

HDRS := barriers/barriers.h barriers/arch-x86.h barriers/arch-arm64.h barriers/arch-arm32.h

TSO_BINS := sb_tso_relaxed sb_tso_mb
RMO_BINS := mp_rmo_none mp_rmo_wmb mp_rmo_rmb mp_rmo_both
SPSC_BINS := spsc_rmo_none spsc_rmo_wmb spsc_rmo_rmb spsc_rmo_both
BINS := $(TSO_BINS) $(RMO_BINS) $(SPSC_BINS)

all: $(BINS) selftest_barriers

# --- TSO / Store-Buffering ---
sb_tso_relaxed: sb_tso.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_MB=0 -Ibarriers -o $@ sb_tso.c $(LDLIBS)

sb_tso_mb: sb_tso.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_MB=1 -Ibarriers -o $@ sb_tso.c $(LDLIBS)

# --- RMO / Message-Passing (4 barrier combos: USE_WMB x USE_RMB) ---
mp_rmo_none: mp_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=0 -DUSE_RMB=0 -Ibarriers -o $@ mp_rmo.c $(LDLIBS)

mp_rmo_wmb: mp_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=1 -DUSE_RMB=0 -Ibarriers -o $@ mp_rmo.c $(LDLIBS)

mp_rmo_rmb: mp_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=0 -DUSE_RMB=1 -Ibarriers -o $@ mp_rmo.c $(LDLIBS)

mp_rmo_both: mp_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=1 -DUSE_RMB=1 -Ibarriers -o $@ mp_rmo.c $(LDLIBS)

# --- RMO / SPSC (continuous-loop MP, 4 barrier combos: USE_WMB x USE_RMB) ---
spsc_rmo_none: spsc_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=0 -DUSE_RMB=0 -Ibarriers -o $@ spsc_rmo.c $(LDLIBS)

spsc_rmo_wmb: spsc_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=1 -DUSE_RMB=0 -Ibarriers -o $@ spsc_rmo.c $(LDLIBS)

spsc_rmo_rmb: spsc_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=0 -DUSE_RMB=1 -Ibarriers -o $@ spsc_rmo.c $(LDLIBS)

spsc_rmo_both: spsc_rmo.c $(HDRS)
	$(CC) $(CFLAGS) -DUSE_WMB=1 -DUSE_RMB=1 -Ibarriers -o $@ spsc_rmo.c $(LDLIBS)

selftest_barriers: selftest_barriers.c $(HDRS)
	$(CC) $(CFLAGS) -Ibarriers -o $@ selftest_barriers.c

run: $(TSO_BINS)
	./run_tso.sh $(N)

check: selftest_barriers $(TSO_BINS)
	./selftest_barriers && ./run_tso.sh $(N)

run-rmo: $(RMO_BINS)
	./run_rmo.sh $(N)

check-rmo: selftest_barriers $(RMO_BINS)
	./selftest_barriers && ./run_rmo.sh $(N)

run-spsc: $(SPSC_BINS)
	./run_spsc.sh $(N)

check-spsc: selftest_barriers $(SPSC_BINS)
	./selftest_barriers && ./run_spsc.sh $(N)

clean:
	rm -f $(BINS) selftest_barriers

.PHONY: all run check run-rmo check-rmo run-spsc check-spsc clean
