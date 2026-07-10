#!/bin/sh
# Run all 4 SPSC barrier combinations and print a comparison table.
#   none (wmb=0,rmb=0)  wmb (1,0)  rmb (0,1)  both (1,1)
# P-side tests wmb (store-store ①); C-side tests rmb (load-load ②).
# By pure RMO only 'both' forbids d[tail]!=tail; none/wmb/rmb should leak.
# Hard invariant: 'both' must forbid on ALL archs (exit 1 if leak).
# On Cortex-A9: ① unobservable (store buffer FIFO) -> 'rmb' may read 0
#   (v7 quirk, flagged); ② observable -> none/wmb should leak.
# On x86 (TSO) all four are 0.
N=${1:-10000000}
ARCH=$(uname -m)

echo "N = $N   arch = $ARCH"
printf "%-8s %18s %10s   %s\n" variant "d[tail]!=tail" rate verdict

f_of() { echo "$1" | awk '{print $2}'; }
r_of() { echo "$1" | awk '{print $4}'; }

o_none=$(./spsc_rmo_none "$N"); f_none=$(f_of "$o_none"); r_none=$(r_of "$o_none")
o_wmb=$(./spsc_rmo_wmb  "$N"); f_wmb=$(f_of "$o_wmb");   r_wmb=$(r_of "$o_wmb")
o_rmb=$(./spsc_rmo_rmb  "$N"); f_rmb=$(f_of "$o_rmb");   r_rmb=$(r_of "$o_rmb")
o_both=$(./spsc_rmo_both "$N"); f_both=$(f_of "$o_both"); r_both=$(r_of "$o_both")

case "$ARCH" in
  x86_64)
    vrd() { [ "$1" -eq 0 ] && echo "Never (TSO: all ordered)" || echo "UNEXPECTED: $1"; }
    ;;
  aarch64|arm*)
    vrd() {
      case "$2" in
        both)
          [ "$1" -eq 0 ] && echo "Never (hard invariant)" || echo "ERROR: both leaked ($1)"
          ;;
        rmb)
          [ "$1" -gt 0 ] && echo "Sometimes (P-side store-store leak)" || echo "WARNING: 0 (v7 store-store FIFO unobservable)"
          ;;
        none|wmb)
          [ "$1" -gt 0 ] && echo "Sometimes (C-side load-load leak)" || echo "WARNING: 0 (v7 load not reordering / N too small)"
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
