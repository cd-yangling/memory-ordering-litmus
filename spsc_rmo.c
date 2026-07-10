/*                            _ooOoo_
                            o8888888o
                             88" . "88
                             (| -_- |)
                              O\ = /O
                          ____/`---'\____
                        .   ' \\| |// `.
                         / \\||| : |||// \
                       / _||||| -:- |||||- \
                         | | \\\ - /// | |
                       | \_| ''\---/'' | |
                        \ .-\__ `-` ___/-. /
                     ___`. .' /--.--\ `. . __
                  ."" '< `.___\_<|>_/___.' >'"".
                 | | : `- \`.;`\ _ /`;.`/ - ` : | |
                   \ \ `-. \_ __\ /__ _/ .-` / /
           ======`-.____`-.___\_____/___.-`____.-'======
                              `=---='

           .............................................
                    佛祖保佑             永无BUG
            佛曰:
                    写字楼里写字间，写字间里程序员；
                    程序人员写程序，又拿程序换酒钱。
                    酒醒只在网上坐，酒醉还来网下眠；
                    酒醉酒醒日复日，网上网下年复年。
                    但愿老死电脑间，不愿鞠躬老板前；
                    奔驰宝马贵者趣，公交自行程序员。
                    别人笑我忒疯癫，我笑自己命太贱；
                    不见满街漂亮妹，哪个归得程序员？
*/
/**
 *	spsc_rmo.c
 *
 *	Copyright (C) 2026 YangLing
 *
 *	Description:
 *
 *	Revision History:
 *
 *	2026-07-10 Created By YangLing (yl.tienon@gmail.com)
 */

/*
 * spsc_rmo.c — SPSC (single-producer/single-consumer) ring queue,
 *              the continuous-loop Message-Passing test (RMO, 4 barrier combos).
 *
 *   Producer: WRITE_ONCE(data[head%SIZE], head); [smp_wmb()?] WRITE_ONCE(head_pub, head+1);
 *   Consumer: while (READ_ONCE(head_pub) == tail) ; [smp_rmb()?]
 *             if (READ_ONCE(data[tail%SIZE]) != tail) forbidden++;
 *
 * Each message is one MP: producer W(data)->[wmb]->W(head) == P0 W(buf)->[wmb]->W(flag);
 * consumer R(head)->[rmb]->R(data) == P1 R(flag)->[rmb]->R(buf). Continuous async
 * pipeline (NO per-round pthread_barrier), N messages, data-integrity check
 * d[tail]==tail (absolute index, wraparound-safe).
 *
 * P-side tests wmb (store-store); C-side tests rmb (load-load).
 * 4 combos (USE_WMB x USE_RMB): none/wmb/rmb/both. By pure RMO only 'both'
 * forbids; the other three should leak. On Cortex-A9, store-store is
 * unobservable (store buffer FIFO) so 'rmb' may read 0; load-load is
 * observable so none/wmb should leak. On x86 (TSO) all four are 0.
 *
 * Compile 4x with -DUSE_WMB={0,1} -DUSE_RMB={0,1}. Uses only barriers.h.
 * SPSC_CPU0 / SPSC_CPU1 env vars override pinning (default 0/1).
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "barriers.h"

#ifndef USE_WMB
#define USE_WMB 0
#endif

#ifndef USE_RMB
#define USE_RMB 1
#endif

#define SIZE 1024

/* Absolute indices 0..N: use long (aligns with mp_rmo's long N, avoids boundary). */
struct linevar_long { long v; char pad[CACHE_LINE_SIZE - sizeof(long)]; };

static long data[SIZE];
static struct linevar_long ____cacheline_internodealigned_in_smp head_pub;
static struct linevar_long ____cacheline_internodealigned_in_smp tail_pub;

static long N = 10000000;
static int cpu0 = 0, cpu1 = 1;

static void pin_cpu(int cpu)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0)
		perror("sched_setaffinity");
}

/* Producer: W(data)->[wmb?]->W(head_pub). Writes only, no counting. */
static void *worker0(void *arg)
{
	(void)arg;
	pin_cpu(cpu0);

	long head = 0;                                      /* producer-private */
	for (long i = 0; i < N; i++) {
		while ((head + 1) % SIZE == READ_ONCE(tail_pub.v) % SIZE)   /* full: spin */
			;
		WRITE_ONCE(data[head % SIZE], head);             /* d[head] = head */
#if USE_WMB
		smp_wmb();                       /* store-store: data before head */
#endif
		head++;
		WRITE_ONCE(head_pub.v, head);                    /* publish */
	}

	return NULL;
}

/* Consumer: R(head_pub)->[rmb?]->R(data); count d[tail]!=tail. */
static void *worker1(void *arg)
{
	(void)arg;
	pin_cpu(cpu1);
	long forbidden = 0;

	long tail = 0;                                      /* consumer-private */
	for (long i = 0; i < N; i++) {
		while (READ_ONCE(head_pub.v) == tail)           /* empty: spin */
			;
#if USE_RMB
		smp_rmb();                       /* load-load: head before data */
#endif
		long val = READ_ONCE(data[tail % SIZE]);
		if (val != tail)
			forbidden++;
		tail++;
		WRITE_ONCE(tail_pub.v, tail);                    /* publish: slot freed */
	}

	long *r = malloc(sizeof(long));
	if (r) *r = forbidden;
	return r;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		N = atol(argv[1]);
	if (N <= 0) {
		fprintf(stderr, "N must be positive\n");
		return 2;
	}

	const char *e0 = getenv("SPSC_CPU0");
	const char *e1 = getenv("SPSC_CPU1");
	if (e0) cpu0 = atoi(e0);
	if (e1) cpu1 = atoi(e1);

	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu < 2)
		fprintf(stderr,
			"WARNING: only %ld online CPU(s); need >=2 to observe "
			"hardware reordering (pinning cpu%d/cpu%d anyway).\n",
			ncpu, cpu0, cpu1);

	pthread_t t0, t1;
	if (pthread_create(&t0, NULL, worker0, NULL) ||
	    pthread_create(&t1, NULL, worker1, NULL)) {
		perror("pthread_create");
		return 2;
	}

	pthread_join(t0, NULL);
	long *r1p = NULL;
	pthread_join(t1, (void **)&r1p);

	long forbidden = r1p ? *r1p : -1;
	free(r1p);

	const char *variant =
#if USE_WMB && USE_RMB
		"both";
#elif USE_WMB
		"wmb";
#elif USE_RMB
		"rmb";
#else
		"none";
#endif
	double rate = 100.0 * (double)forbidden / (double)N;
	printf("%s %ld %ld %.6f\n", variant, forbidden, N, rate);
	return 0;
}
