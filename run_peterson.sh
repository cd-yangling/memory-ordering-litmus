#!/bin/sh
# Run Peterson lock correctness test (2 variants: none vs mb_both).
#   none:     relaxed, Peterson may fail (count < 2N)
#   mb_both:  full barrier at enter+exit, should be correct (count == 2N)
N=${1:-10000000}
ARCH=$(uname -m)

echo "N = $N   arch = $ARCH"
printf "%-8s %18s %10s   %s\n" variant "count/expected" rate verdict

# Parse output: variant count expected rate verdict
count_of() { echo "$1" | awk '{print $2}'; }
rate_of()  { echo "$1" | awk '{print $4}'; }

o_none=$(./peterson_none "$N")
o_mb_both=$(./peterson_mb_both "$N")

c_none=$(count_of "$o_none")
r_none=$(rate_of "$o_none")

c_mb_both=$(count_of "$o_mb_both")
r_mb_both=$(rate_of "$o_mb_both")

expected=$((2 * N))

verdict() {
  if [ "$1" -eq "$2" ]; then
    echo "PASS"
  else
    echo "FAIL"
  fi
}

printf "%-8s %14s/%s %8s%%   %s\n" none     "$c_none"     "$expected" "$r_none"     "$(verdict "$c_none"     "$expected")"
printf "%-8s %14s/%s %8s%%   %s\n" mb_both  "$c_mb_both"  "$expected" "$r_mb_both"  "$(verdict "$c_mb_both"  "$expected")"

# Hard invariant: mb_both must pass on all archs.
[ "$c_mb_both" -eq "$expected" ] || exit 1