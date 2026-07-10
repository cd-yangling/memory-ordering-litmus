# Memory Ordering Litmus Tests

用 pthreads 实现的内存序 litmus 测试,在 x86(TSO)和 ARMv7(Cortex-A9,RMO)上跑同一份代码,观察硬件内存重排并用屏障消除。`barriers/` 是 arch 抽象层,x86_64/aarch64/armv7 各一个头文件。

三个测试:

- `sb_tso.c` — Store-Buffering,TSO 的 store-load 重排,smp_mb 消除
- `mp_rmo.c` — Message-Passing,4 屏障组合(wmb×rmb),store-store + load-load
- `spsc_rmo.c` — SPSC 环形队列,持续循环 MP,P 端测 wmb / C 端测 rmb

## 构建

    make            # 编所有变体 + selftest_barriers
    make run        # SB
    make run-rmo    # MP 4 组合
    make run-spsc   # SPSC 4 组合

按 `uname -m` 自动选 arch(x86_64 / aarch64)。armv7 需交叉编译,见下文。

## 1. SB / Store-Buffering(TSO)

TSO 的签名行为:store-load 重排。

    P0: WRITE_ONCE(*x,1); [smp_mb()?] r0=READ_ONCE(*y);
    P1: WRITE_ONCE(*y,1); [smp_mb()?] r1=READ_ONCE(*x);
    问:能否 0:r0=0 /\ 1:r1=0 ?

relaxed 会出现 0:0(store buffer 致 store-load 重排),smp_mb 消除。

    ./run_tso.sh 10000000

x86_64 样例:

    N = 10000000   arch = x86_64
    variant        0:0 outcomes       rate   verdict
    relaxed            1381/10000000 0.013810%   Sometimes (reordering observed)
    smp_mb                0/10000000 0.000000%   Never (barrier OK)

## 2. MP / Message-Passing(4 屏障组合)

    P0: WRITE_ONCE(*buf,1); [smp_wmb()?] WRITE_ONCE(*flag,1);
    P1: r0=READ_ONCE(*flag); [smp_rmb()?] r1=READ_ONCE(*buf);
    问: 1:r0=1 /\ 1:r1=0 ?

forbidden(看到 flag=1 却没看到 buf)两个来源:P0 store-store 重排(被 wmb 封)、P1 load-load 重排(被 rmb 封)。4 组合:

| 变体 | wmb | rmb |
|---|---|---|
| none | 0 | 0 |
| wmb | 1 | 0 |
| rmb | 0 | 1 |
| both | 1 | 1 |

`mp_rmo.c` 默认 `USE_SS_PRIME=1`:P1 每轮预读 buf,让其进 CPU0 S 状态;P0 写 buf(a)时 S→BusUgr(慢),写 flag(b)时 M write-hit(快),制造 store-store 不对称。`-DUSE_SS_PRIME=0` 关闭回 reset 基线。

    make run-rmo

ARMv7(Cortex-A9)样例(20M,默认 prime):

    N = 20000000   arch = armv7l
    variant   1:r0=1,1:r1=0       rate   verdict
    none            886/20000000 0.004430%   Sometimes (SS+LL)
    wmb               1/20000000 0.000005%   Sometimes (LL 偶发, wmb 不封 LL)
    rmb             130/20000000 0.000650%   Sometimes (SS via SS_PRIME)
    both              0/20000000 0.000000%   Never (hard invariant)

x86(TSO)4 组合全 0(load-load、store-store 均有序)。

## 3. SPSC / 持续循环 MP

`spsc_rmo.c` 是 mp_rmo 的持续循环版:1024 槽环形队列,生产者持续写、消费者持续读,每条消息都是一次 MP。P 端测 wmb(store-store)、C 端测 rmb(load-load),4 组合。

    Producer: WRITE_ONCE(data[head%SIZE], head); [smp_wmb()?] WRITE_ONCE(head_pub, head+1);
    Consumer: while (READ_ONCE(head_pub) == tail) ; [smp_rmb()?]
              if (READ_ONCE(data[tail%SIZE]) != tail) forbidden++;

无每轮 barrier,持续异步流水。校验 `d[tail]==tail`(绝对值,回绕安全)。

    make run-spsc

ARMv7 样例(10M):

    N = 10000000   arch = armv7l
    variant   d[tail]!=tail       rate   verdict
    none          5431/10000000 0.054310%   Sometimes (load-load leak)
    wmb              0/10000000 0.000000%   WARNING: 0 (wmb 时序副作用压 LL, 非保证)
    rmb             217/10000000 0.002170%   Sometimes (store-store leak)
    both              0/10000000 0.000000%   Never (hard invariant)

SPSC 比 mp_rmo 单发放大 load-load ~200x(0.00025% → 0.054%);多槽结构(head write-hit / data write-miss)还浮现了 store-store(rmb=217),这是 mp_rmo reset 基线抓不到的。

## ARMv7(Cortex-A9)内存模型

四种重排:

| reorder | 状态 | 来源 |
|---|---|---|
| store-load | 实测 | sb_tso relaxed 0.175% |
| store-store | 实测 | spsc rmb=217、mp_rmo prime rmb=130 |
| load-load | 实测 | mp_rmo/spsc none |
| load-store | 理论允许 | 未构造 litmus |

ARMv7 是 RMO。store-store 在 reset 结构下不可观测(store buffer FIFO + cache 热),需 SPSC 多槽或 SS_PRIME 预热制造 head/data 可见性差异才浮现。wmb 封 store-store、rmb 封 load-load、mb 封 store-load,`both`(wmb+rmb)是语义硬不变量。

## barriers 抽象层

| 原语 | x86 | arm64 | armv7 |
|---|---|---|---|
| smp_mb | mfence | dmb ish | dmb ish |
| smp_rmb | barrier() | dmb ishld | dmb ish |
| smp_wmb | barrier() | dmb ishst | dmb ishst |
| CACHE_LINE_SIZE | 64 | 128 | 64 |

armv7 没有 `dmb ishld`(ARMv8 才有),smp_rmb 退用 `dmb ish`(稍强,正确)。

## 交叉编译(ARMv7)

    GCC=<arm-linux-musleabi-gcc 路径>
    for V in none wmb rmb both; do
      case $V in none) W=0 R=0;; wmb) W=1 R=0;; rmb) W=0 R=1;; both) W=1 R=1;; esac
      "$GCC" -static -O2 -Ibarriers -march=armv7-a -mfloat-abi=soft -marm -mno-unaligned-access \
        -DUSE_WMB=$W -DUSE_RMB=$R -o mp_rmo_$V mp_rmo.c -lpthread
    done

设备需 ≥2 核 SMP 才能观测硬件重排。

## 参考资料

- Linux 内核源码 — <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/>  
  `barriers/` 的屏障原语(lock addl / dmb ish / dmb ishst 等)派生自 `arch/{x86,arm64,arm}/include/asm/barrier.h`(GPL v2)
- Documentation/memory-barriers.txt — <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/memory-barriers.txt>  
  内存屏障语义、litmus 模型与各种 reorder 的说明
- perfbook(Paul McKenney)— <https://git.kernel.org/pub/scm/linux/kernel/git/paulmck/perfbook.git/>  
  *Is Parallel Programming Hard, And, If So, What Can You Do About It?*,`CodeSamples/` 提供 arch 抽象层与 litmus 示例
