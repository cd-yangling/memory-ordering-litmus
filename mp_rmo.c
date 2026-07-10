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
 *	mp_rmo.c
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
 * mp_rmo.c — Message-Passing (MP) litmus test, 4 barrier combinations.
 *
 *   P0: WRITE_ONCE(*buf,1); [smp_wmb()?] WRITE_ONCE(*flag,1);
 *   P1: r0=READ_ONCE(*flag); [smp_rmb()?] r1=READ_ONCE(*buf);
 *   Question: 1:r0=1 /\ 1:r1=0 ?
 *
 * forbidden (r0=1 /\ r1=0) has two sources:
 *   (1) P0 store-store reorder  — stopped by smp_wmb
 *   (2) P1 load-load reorder    — stopped by smp_rmb
 *
 * 4 combinations (USE_WMB x USE_RMB):
 *   none (0,0)  wmb (1,0)  rmb (0,1)  both (1,1)
 *
 * On Cortex-A9 (store buffer FIFO + OOO loads): (1) unobservable, (2) observable.
 *   => none/wmb leak (load-load); rmb/both forbid. 'both' is the hard invariant.
 * On x86 (TSO): all four 0 (load-load + store-store both ordered).
 *
 * Compile 4x with -DUSE_WMB={0,1} -DUSE_RMB={0,1}. Uses only barriers.h.
 * P1 owns both loads and counts the forbidden outcome.
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

#ifndef USE_SS_PRIME
#define USE_SS_PRIME 1
#endif

/* Each shared var on its own cache line (no false sharing). */
struct linevar { int v; char pad[CACHE_LINE_SIZE - sizeof(int)]; };

static struct linevar ____cacheline_internodealigned_in_smp buf;
static struct linevar ____cacheline_internodealigned_in_smp flag;

static pthread_barrier_t bar;
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

/* P0: store buf, [wmb?], store flag. Writes only, no counting. */
static void *worker0(void *arg)
{
	(void)arg;
	pin_cpu(cpu0);

	for (long i = 0; i < N; i++) {
		WRITE_ONCE(buf.v, 0);
		WRITE_ONCE(flag.v, 0);
		pthread_barrier_wait(&bar);          /* B1: reset visible */

		WRITE_ONCE(buf.v, 1);
#if USE_WMB
		smp_wmb();
#endif
		WRITE_ONCE(flag.v, 1);
		pthread_barrier_wait(&bar);          /* B2: sync next round */
	}

	return NULL;
}

/* P1: load flag, smp_rmb (fixed fixture), load buf; count forbidden. */
static void *worker1(void *arg)
{
	(void)arg;
	pin_cpu(cpu1);
	long forbidden = 0;

	for (long i = 0; i < N; i++) {
		pthread_barrier_wait(&bar);          /* B1 */

#if USE_SS_PRIME
		/* Prime buf into CPU0 S so worker0's write goes S->BusUgr (slow)
		   while flag stays M write-hit (fast) -> store-store window. */
		(void)READ_ONCE(buf.v);
#endif
		int r0 = READ_ONCE(flag.v);
#if USE_RMB
		smp_rmb();                           /* read-side fixture: isolates store-store sig */
#endif
		int r1 = READ_ONCE(buf.v);
		if (r0 == 1 && r1 == 0)
			forbidden++;
		pthread_barrier_wait(&bar);          /* B2 */
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

	const char *e0 = getenv("MP_CPU0");
	const char *e1 = getenv("MP_CPU1");
	if (e0) cpu0 = atoi(e0);
	if (e1) cpu1 = atoi(e1);

	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu < 2)
		fprintf(stderr,
			"WARNING: only %ld online CPU(s); need >=2 to observe "
			"hardware reordering (pinning cpu%d/cpu%d anyway).\n",
			ncpu, cpu0, cpu1);

	if (pthread_barrier_init(&bar, NULL, 2) != 0) {
		perror("pthread_barrier_init");
		return 2;
	}

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
	pthread_barrier_destroy(&bar);

	const char *variant =
#if USE_WMB && USE_RMB
		USE_SS_PRIME ? "both+prime" : "both";
#elif USE_WMB
		USE_SS_PRIME ? "wmb+prime" : "wmb";
#elif USE_RMB
		USE_SS_PRIME ? "rmb+prime" : "rmb";
#else
		USE_SS_PRIME ? "none+prime" : "none";
#endif
	double rate = 100.0 * (double)forbidden / (double)N;
	printf("%s %ld %ld %.6f\n", variant, forbidden, N, rate);
	return 0;
}
