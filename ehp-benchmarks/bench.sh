file=${1}

echo benchmarks for "${file}"
echo "with the watchdog"
time perf stat -r 50 -d watchdog_node/node "${file}"

echo ""
echo ""
echo "without the watchdog"
time perf stat -r 50 -d original_node/node "${file}"

