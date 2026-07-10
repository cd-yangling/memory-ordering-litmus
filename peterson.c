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
 *	peterson.c
 *
 *	Copyright (C) 2026 YangLing
 *
 *	Description: Peterson lock litmus test
 *
 *	Revision History:
 *
 *	2026-07-10 Created By YangLing (yl.tienon@gmail.com)
 */

/*
 * peterson.c — Peterson lock correctness test.
 *
 *   Peterson lock as mutex: two threads, each loops N times:
 *     peterson_lock(); count++; peterson_unlock();
 *
 *   Verdict: count == 2N (correct) or count < 2N (lock failed).
 *
 *   Barrier variants:
 *     none (USE_MB=0):    relaxed, Peterson may fail
 *     mb_both (USE_MB=1): full barrier at enter+exit, should be correct
 *
 * Compile:
 *   gcc -DUSE_MB={0,1} -O2 -Ibarriers -o peterson_{none,mb_both} peterson.c -lpthread
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

/*
 * Peterson lock: flag[0]/flag[1]/turn each on separate cache line.
 * Cache-line isolation avoids false sharing noise.
 */
struct peterson_lock {
	struct { volatile int v; char pad[CACHE_LINE_SIZE - sizeof(int)]; } flag[2];
	struct { volatile int v; char pad[CACHE_LINE_SIZE - sizeof(int)]; } turn;
};

/* Shared state */
static struct peterson_lock lock;
static volatile int count = 0;
static long N = 10000000;
static pthread_barrier_t start_barrier;
static int cpu0 = 0, cpu1 = 1;

static void pin_cpu(int cpu)
{
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0)
		perror("sched_setaffinity");
}

static inline void peterson_lock(struct peterson_lock *l, int id)
{
	l->flag[id].v = 1;
	l->turn.v = 1 - id;           /* yield to other */
#if USE_MB
	smp_mb();                     /* enter barrier */
#endif
	while (l->flag[1-id].v == 1 && l->turn.v == 1 - id)
		;                         /* spin */
}

static inline void peterson_unlock(struct peterson_lock *l, int id)
{
#if USE_MB
	smp_mb();                     /* exit barrier */
#endif
	l->flag[id].v = 0;
}

static void *worker(void *arg)
{
	int id = *(int *)arg;
	pin_cpu(id);

	pthread_barrier_wait(&start_barrier);  /* simultaneous start */

	for (long i = 0; i < N; i++) {
		peterson_lock(&lock, id);
		count++;
		peterson_unlock(&lock, id);
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

	/* CPU pinning (optional env override) */
	const char *e0 = getenv("PETERSON_CPU0");
	const char *e1 = getenv("PETERSON_CPU1");
	if (e0) cpu0 = atoi(e0);
	if (e1) cpu1 = atoi(e1);

	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu < 2)
		fprintf(stderr,
			"WARNING: only %ld online CPU(s); need >=2 to observe "
			"Peterson failure (pinning cpu%d/cpu%d anyway).\n",
			ncpu, cpu0, cpu1);

	/* Initialize lock: all zero */
	lock.flag[0].v = 0;
	lock.flag[1].v = 0;
	lock.turn.v = 0;
	count = 0;

	if (pthread_barrier_init(&start_barrier, NULL, 2) != 0) {
		perror("pthread_barrier_init");
		return 2;
	}

	int id0 = 0, id1 = 1;
	pthread_t t0, t1;
	if (pthread_create(&t0, NULL, worker, &id0) ||
	    pthread_create(&t1, NULL, worker, &id1)) {
		perror("pthread_create");
		return 2;
	}

	pthread_join(t0, NULL);
	pthread_join(t1, NULL);

	pthread_barrier_destroy(&start_barrier);

	long expected = 2 * N;
	const char *variant = USE_MB ? "mb_both" : "none";
	const char *verdict = (count == expected) ? "PASS" : "FAIL";
	double rate = (double)(expected - count) / (double)expected;

	printf("%s %d %ld %.6f %s\n", variant, count, expected, rate, verdict);

	return (count == expected) ? 0 : 1;
}