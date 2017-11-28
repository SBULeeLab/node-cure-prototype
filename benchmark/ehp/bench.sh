file=${1}

echo benchmarks for "${file}"

echo "vanilla"
time perf stat -r 500 --pre "sync && sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches' && sync" -d nodes/original_node/node "${file}"

echo ""
echo ""
echo "lazy watchdog"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy
time perf stat -r 500 --pre "sync && sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches' && sync" -d nodes/watchdog_node/node "${file}"

echo ""
echo ""
echo "precise watchdog"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=precise
time perf stat -r 500 --pre "sync && sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches' && sync" -d nodes/watchdog_node/node "${file}"
