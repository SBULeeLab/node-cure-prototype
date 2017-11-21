file=${1}

echo benchmarks for "${file}"
echo "with the watchdog"
time perf stat -r 50 -d node-cure/node "${file}"

echo ""
echo ""
echo "without the watchdog"
time perf stat -r 50 -d orig_without_changes/node "${file}"

