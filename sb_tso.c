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
 *	sb_tso.c
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
 * sb_tso.c — Store-Buffering (SB) litmus test, the TSO signature.
 *
 *   P0: WRITE_ONCE(*x,1); [smp_mb()?] r0=READ_ONCE(*y);
 *   P1: WRITE_ONCE(*y,1); [smp_mb()?] r1=READ_ONCE(*x);
 *   Question: 0:r0=0 /\ 1:r1=0 ?
 *     relaxed (USE_MB=0) -> Sometimes
 *     smp_mb  (USE_MB=1) -> Never
 *
 * Compile twice with -DUSE_MB=0 and -DUSE_MB=1. Uses only barriers.h.
 * Each thread is its own function (worker0/worker1) so the per-thread
 * memory-ordering story is explicit — no branching in the hot loop.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "barriers.h"

#ifndef USE_MB
#define USE_MB 0
#endif

/* Each shared var on its own cache line (no false sharing). */
struct linevar { int v; char pad[CACHE_LINE_SIZE - sizeof(int)]; };

static struct linevar ____cacheline_internodealigned_in_smp x;
static struct linevar ____cacheline_internodealigned_in_smp y;
static struct linevar ____cacheline_internodealigned_in_smp r1_pub;

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

/* P0: store x, load y; count the forbidden 0:0 outcome. */
static void *worker0(void *arg)
{
	(void)arg;
	pin_cpu(cpu0);
	long forbidden = 0;

	for (long i = 0; i < N; i++) {
		WRITE_ONCE(x.v, 0);
		pthread_barrier_wait(&bar);          /* B1: reset visible */

		WRITE_ONCE(x.v, 1);
#if USE_MB
		smp_mb();
#endif
		int r0 = READ_ONCE(y.v);
		pthread_barrier_wait(&bar);          /* B2: r1_pub visible */
		if (r0 == 0 && READ_ONCE(r1_pub.v) == 0)
			forbidden++;
	}

	long *r = malloc(sizeof(long));
	if (r) *r = forbidden;
	return r;
}

/* P1: store y, load x; publish r1 for P0 to check. */
static void *worker1(void *arg)
{
	(void)arg;
	pin_cpu(cpu1);

	for (long i = 0; i < N; i++) {
		WRITE_ONCE(y.v, 0);
		pthread_barrier_wait(&bar);          /* B1 */

		WRITE_ONCE(y.v, 1);
#if USE_MB
		smp_mb();
#endif
		int r1 = READ_ONCE(x.v);
		WRITE_ONCE(r1_pub.v, r1);
		pthread_barrier_wait(&bar);          /* B2 */
	}

	return NULL;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		N = atol(argv[1]);
	if (N <= 0) {
		fprintf(stderr, "N must be positive\n");
		return 2;
	}

	const char *e0 = getenv("SB_CPU0");
	const char *e1 = getenv("SB_CPU1");
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

	long *r0p = NULL;
	pthread_join(t0, (void **)&r0p);
	pthread_join(t1, NULL);

	long forbidden = r0p ? *r0p : -1;
	free(r0p);
	pthread_barrier_destroy(&bar);

	const char *variant = USE_MB ? "smp_mb" : "relaxed";
	double rate = 100.0 * (double)forbidden / (double)N;
	printf("%s %ld %ld %.6f\n", variant, forbidden, N, rate);
	return 0;
}
