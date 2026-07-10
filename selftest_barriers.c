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
 *	selftest_barriers.c
 *
 *	Copyright (C) 2026 YangLing
 *
 *	Description:
 *
 *	Revision History:
 *
 *	2026-07-10 Created By YangLing (yl.tienon@gmail.com)
 */

/* selftest_barriers.c: deterministic checks for the barrier abstraction. */
#include <assert.h>
#include <stdio.h>
#include "barriers.h"

int main(void)
{
	int a = 0, b = 0;

	/* READ_ONCE / WRITE_ONCE round-trip. */
	WRITE_ONCE(a, 42);
	assert(READ_ONCE(a) == 42);

	/* smp_store_release / smp_load_acquire round-trip. */
	smp_store_release(&b, 7);
	assert(smp_load_acquire(&b) == 7);

	/* CACHE_LINE_SIZE matches the arch we compiled for. */
#if defined(__x86_64__)
	assert(CACHE_LINE_SIZE == 64);
#elif defined(__aarch64__)
	assert(CACHE_LINE_SIZE == 128);
#elif defined(__arm__)
	assert(CACHE_LINE_SIZE == 64);
#endif

	/* Barriers compile and are callable. */
	smp_mb();
	smp_rmb();
	smp_wmb();

	/* An aligned object actually lands on a cache line. */
	struct { int v; } __attribute__((aligned(CACHE_LINE_SIZE))) aligned_obj = { 0 };
	assert(((unsigned long)&aligned_obj) % CACHE_LINE_SIZE == 0);

	printf("selftest_barriers: OK (arch=%s, CACHE_LINE_SIZE=%d)\n",
#if defined(__x86_64__)
	       "x86_64",
#elif defined(__aarch64__)
	       "aarch64",
#elif defined(__arm__)
	       "armv7",
#else
	       "unknown",
#endif
	       CACHE_LINE_SIZE);
	return 0;
}
