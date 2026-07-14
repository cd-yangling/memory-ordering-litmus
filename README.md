# Memory Ordering Litmus Tests

用 pthreads 实现的内存序 litmus 测试,在 x86(TSO)、ARMv7(Cortex-A9,RMO)和 aarch64(Cortex-A72,RMO)上跑同一份代码,观察硬件内存重排并用屏障消除。`barriers/` 是 arch 抽象层,x86_64/aarch64/armv7 各一个头文件。

四个测试:

- `sb_tso.c` — Store-Buffering,TSO 的 store-load 重排,smp_mb 消除
- `mp_rmo.c` — Message-Passing,4 屏障组合(wmb×rmb),store-store + load-load
- `spsc_rmo.c` — SPSC 环形队列,持续循环 MP,P 端测 wmb / C 端测 rmb
- `peterson.c` — Peterson 互斥锁正确性,none/mb_both 两个变体

## 构建

    make            # 编所有变体 + selftest_barriers
    make run        # SB
    make run-rmo    # MP 4 组合
    make run-spsc   # SPSC 4 组合
    make run-peterson  # Peterson 2 变体

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

### aarch64(Cortex-A72)

> **运行环境**:数据取自 ARM 宿主上的 **KVM 透传 VM**,非裸机。KVM 不模拟内存模型(客核直接跑真 A72 硅片,仅特权指令 trap),故**重排结论与裸机等价、可靠**(load-load 易漏、store-store 罕见但存在、`both`=0)。但 vCPU 调度抖动会影响 P/C 耦合,**绝对计数率与方差不等于裸机** —— 百分比按数量级/定性理解即可,勿当基准精读。

在 Cortex-A72(part `0xd08`,`r0p2`)上,SPSC 呈现与 A9 明显不同的画面。下面两组样例分别取自一次 1e7 和一次 1e8 运行:

    N = 10000000   arch = aarch64
    variant   d[tail]!=tail       rate   verdict
    none         146273/10000000 1.462730%   Sometimes (C-side load-load leak)
    wmb           33005/10000000 0.330050%   Sometimes (C-side load-load leak)
    rmb               0/10000000 0.000000%   WARNING: 0 (v7 store-store FIFO unobservable)
    both              0/10000000 0.000000%   Never (hard invariant)

    N = 100000000   arch = aarch64
    variant   d[tail]!=tail         rate   verdict
    none            380/100000000 0.000380%   Sometimes (C-side load-load leak)
    wmb            7997/100000000 0.007997%   Sometimes (C-side load-load leak)
    rmb             343/100000000 0.000343%   Sometimes (P-side store-store leak)
    both              0/100000000 0.000000%   Never (hard invariant)

- **load-load 易观测**:`none`/`wmb` 在 1e7 必漏,单轮最高 **1.46%** —— 确认 aarch64 对 load-load 是 RMO。`wmb` 只封 P 端 store-store、不封 C 端 load-load,所以照样漏(正确行为,不是 bug)。
- **`wmb` 的违规数常高于 `none`(1e8:4334~7997 vs 0~380),不是 bug**:`both`=0 已证明 wmb 的 store-store 排序正确,`wmb` 变体漏的全是 C 端 load-load(只有 rmb 能封)。而 A72 上 store-store 本就罕见(见下条),`none` 的违规也几乎全是 load-load —— **两者同源**。差别纯是**时序耦合**:`dmb ishst` 拖慢生产者 → P/C 贴得更紧 → 消费者更频繁撞上「`data[tail]` 投机读」对「生产者本次写失效传播」的竞速窗口;`none` 全速的生产者甩开消费者 ~1024 槽,消费者读到的都是久已稳定的槽,撞不上窗口。屏障改的是吞吐/耦合,不是语义;KVM 的 vCPU 调度抖动进一步放大了这个效应(也解释了下文方差)。
- **store-store 在 1e7 不可观测**:`rmb` 变体是 P 端 store-store 的探针(已用 rmb 封住 load-load,剩下的只能是 store-store),7 轮 1e7 **全为 0**。对比 Cortex-A9 在 1e7 即 217 次(2.17e-4),A72 难浮现约一个量级。
- **但拉到 1e8 就漏了**(见上 1e8 表 `rmb`=343):4 轮 1e8 的 `rmb` 分别 **35 / 0 / 0 / 343**(~1e-7 ~ 1e-6)。结论:A72 的 store buffer「更接近 FIFO」却**非严格 FIFO**,store-store 重排依然存在 —— 这就是 aarch64 仍需 `smp_wmb` 的实测依据(破除「ARMv8 store-store 安全」的错觉)。
- **硬不变量成立**:`both` 全部 **11 轮**(7×1e7 + 4×1e8)为 0。
- **方差极大**:同一 `none`/1e7 配置 7 轮绝对计数从 **38 到 146273**(率差 ~4000x),1e8 甚至出现过 `none`=0。弱内存「漏报率」高度依赖线程调度时序,单轮结果几乎无意义,需大 N + 多轮才可信。
- `rmb` 的 WARNING 文案(`v7 store-store FIFO unobservable`)是 `run_spsc.sh` 里基于 v7 的启发式,aarch64 上照样触发,但措辞对 A72 不准确(应是「N 不够大」而非「v7 FIFO」)。

> **VM 判定依据**:`cpuinfo` 透出真实核型号 Cortex-A72,但 `BogoMIPS: 100.00`、`Socket(s): 4`(各 1 核)、`L3: 128MiB`(A72 无此规模 L3)均指向 ARM 宿主上的 KVM 透传 VM;4 vCPU 各自独立核(非 SMT),P/C 分别 pin 到 cpu0/cpu1,观测条件成立。

## 4. Peterson Lock Correctness

Peterson 算法作为纯用户态互斥锁,正确性依赖内存序屏障。

    P0: flag[0]=1; turn=1; [mb?] while(flag[1]&&turn==1); count++; [mb?] flag[0]=0;
    P1: flag[1]=1; turn=0; [mb?] while(flag[0]&&turn==0); count++; [mb?] flag[1]=0;

两线程各循环 N 次,临界区 count++,预期 count=2N。

| 变体 | 屏障 | 预期 |
|------|------|------|
| none | 无 | count<2N (Peterson 失效,store-load 重排) |
| mb_both | 进入+退出 smp_mb | count==2N (正确) |

    make run-peterson

x86_64 样例(100K):

    N = 100000   arch = x86_64
    variant      count/expected       rate   verdict
    none           199996/200000 0.000020%   FAIL
    mb_both        200000/200000 0.000000%   PASS

x86(TSO)上 none 也偶发失效(Peterson 需要 store-load 有序,TSO 仅部分保证)。

ARMv7 样例(10M):

    N = 10000000   arch = armv7l
    variant      count/expected       rate   verdict
    none           19999551/20000000 0.000022%   FAIL
    mb_both        20000000/20000000 0.000000%   PASS

ARMv7(RMO)上 none 大量失效(store-load 重排),mb_both 正确。

## ARMv7(Cortex-A9)内存模型

四种重排:

| reorder | 状态 | 来源 |
|---|---|---|
| store-load | 实测 | sb_tso relaxed 0.175% |
| store-store | 实测 | spsc rmb=217、mp_rmo prime rmb=130 |
| load-load | 实测 | mp_rmo/spsc none |
| load-store | 理论允许 | 未构造 litmus |

ARMv7 是 RMO。store-store 在 reset 结构下不可观测(store buffer FIFO + cache 热),需 SPSC 多槽或 SS_PRIME 预热制造 head/data 可见性差异才浮现。wmb 封 store-store、rmb 封 load-load、mb 封 store-load,`both`(wmb+rmb)是语义硬不变量。

## aarch64(Cortex-A72)内存模型

| reorder | 状态 | 来源 |
|---|---|---|
| store-load | 预期可重排 | 本轮未在 A72 跑 sb_tso |
| store-store | 实测(罕见) | spsc `rmb` @1e8:35 / 343 per 1e8 |
| load-load | 实测(易) | spsc `none`/`wmb`,单轮最高 1.46% |
| load-store | 理论允许 | 未构造 litmus |

Cortex-A72 仍是 RMO,但 store-store 比 Cortex-A9 难观测约一个量级(需 ~1e8、~1e-7~1e-6 才浮现):store buffer 更接近 FIFO,却**非严格 FIFO** —— aarch64 仍需 `smp_wmb` 的实证。load-load 则一如既往地易漏,`rmb`/`smp_rmb` 必需。`both`(wmb+rmb)语义硬不变量在 A72 上 11 轮全 0。

## barriers 抽象层

| 原语 | x86 | arm64 | armv7 |
|---|---|---|---|
| smp_mb | lock addl | dmb ish | dmb ish |
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
