file=${1}

echo benchmarks for "${file}"
echo "with the watchdog"
taskset 0x1 time perf stat -r 500 --pre "sync && sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches' && sync" -d nodes/watchdog_node/node "${file}"

echo ""
echo ""
echo "without the watchdog"
taskset 0x1 time perf stat -r 500 --pre "sync && sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches' && sync" -d nodes/original_node/node "${file}"

