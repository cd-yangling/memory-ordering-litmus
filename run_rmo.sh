#!/bin/sh
# Run all 4 MP barrier combinations and print a comparison table.
#   none (wmb=0,rmb=0)  wmb (1,0)  rmb (0,1)  both (1,1)
# Hard invariant: 'both' must forbid 1:r0=1,1:r1=0 on ALL archs (exit 1 if leak).
# On weak archs (arm) forbidden comes from load-load reorder (RMO sig):
#   none/wmb leak (rmb absent); rmb/both forbid (rmb present).
# On x86 (TSO) all four are 0 (load-load + store-store both ordered).
N=${1:-10000000}
ARCH=$(uname -m)

echo "N = $N   arch = $ARCH"
printf "%-8s %18s %10s   %s\n" variant "1:r0=1,1:r1=0" rate verdict

f_of() { echo "$1" | awk '{print $2}'; }
r_of() { echo "$1" | awk '{print $4}'; }

o_none=$(./mp_rmo_none "$N"); f_none=$(f_of "$o_none"); r_none=$(r_of "$o_none")
o_wmb=$(./mp_rmo_wmb "$N");   f_wmb=$(f_of "$o_wmb");   r_wmb=$(r_of "$o_wmb")
o_rmb=$(./mp_rmo_rmb "$N");   f_rmb=$(f_of "$o_rmb");   r_rmb=$(r_of "$o_rmb")
o_both=$(./mp_rmo_both "$N"); f_both=$(f_of "$o_both"); r_both=$(r_of "$o_both")

case "$ARCH" in
  x86_64)
    vrd() { [ "$1" -eq 0 ] && echo "Never (TSO: all ordered)" || echo "UNEXPECTED: $1"; }
    ;;
  aarch64|arm*)
    # mp_rmo 默认 USE_SS_PRIME=1:rmb 变体 forbidden 来自 store-store(SS 浮现),
    # 非 load-load 泄漏。verdict 按 prime 语义标注。
    vrd() {
      case "$2" in
        both)
          [ "$1" -eq 0 ] && echo "Never (hard invariant)" || echo "ERROR: both leaked ($1)"
          ;;
        rmb)
          [ "$1" -gt 0 ] && echo "Sometimes (SS via SS_PRIME)" || echo "WARNING: 0 (SS 未浮现, check N/cache)"
          ;;
        none)
          [ "$1" -gt 0 ] && echo "Sometimes (SS+LL)" || echo "WARNING: 0 (check N/cache)"
          ;;
        wmb)
          [ "$1" -gt 0 ] && echo "Sometimes (LL 偶发, wmb 不封 LL)" || echo "Never (wmb 时序副作用压 LL, 非保证)"
          ;;
      esac
    }
    ;;
  *)
    vrd() { echo "UNKNOWN arch: $ARCH (f=$1)"; }
    ;;
esac

printf "%-8s %14s/%s %8s%%   %s\n" none "$f_none" "$N" "$r_none" "$(vrd "$f_none" none)"
printf "%-8s %14s/%s %8s%%   %s\n" wmb  "$f_wmb"  "$N" "$r_wmb"  "$(vrd "$f_wmb"  wmb)"
printf "%-8s %14s/%s %8s%%   %s\n" rmb  "$f_rmb"  "$N" "$r_rmb"  "$(vrd "$f_rmb"  rmb)"
printf "%-8s %14s/%s %8s%%   %s\n" both "$f_both" "$N" "$r_both" "$(vrd "$f_both" both)"

# Hard invariant: 'both' (wmb+rmb) forbids on all archs.
[ "$f_both" -eq 0 ] || exit 1
