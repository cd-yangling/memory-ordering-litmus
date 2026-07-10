#!/bin/sh
# Run both SB variants and print a comparison table.
# Hard invariant: the smp_mb variant must forbid 0:0 (exit 1 if it leaks).
N=${1:-10000000}

echo "N = $N   arch = $(uname -m)"
printf "%-8s %18s %10s   %s\n" variant "0:0 outcomes" rate verdict

out_r=$(./sb_tso_relaxed "$N")     # "relaxed <f> <N> <rate>"
out_m=$(./sb_tso_mb "$N")          # "smp_mb  <f> <N> <rate>"

fr=$(echo "$out_r" | awk '{print $2}')
rr=$(echo "$out_r" | awk '{print $4}')
fm=$(echo "$out_m" | awk '{print $2}')
rm=$(echo "$out_m" | awk '{print $4}')

if [ "$fm" -ne 0 ]; then
  vm="ERROR: barrier leaked ($fm)"
else
  vm="Never (barrier OK)"
fi

if [ "$fr" -gt 0 ]; then
  vr="Sometimes (reordering observed)"
else
  vr="WARNING: 0 (check vCPU/pinning/alignment)"
fi

printf "%-8s %14s/%s %8s%%   %s\n" relaxed "$fr" "$N" "$rr" "$vr"
printf "%-8s %14s/%s %8s%%   %s\n" smp_mb  "$fm" "$N" "$rm" "$vm"

# Deterministic invariant: full barrier must forbid the 0:0 outcome.
[ "$fm" -eq 0 ] || exit 1
