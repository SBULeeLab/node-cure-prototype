file=${1}

echo benchmarks for "${file}"
echo "with the watchdog"
time perf stat -r 50 --pre "sync && sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches'" -d watchdog_node/node "${file}"

echo ""
echo ""
echo "without the watchdog"
time perf stat -r 50 --pre "sync && sudo sh -c 'echo 3 >/proc/sys/vm/drop_caches'" -d original_node/node "${file}"

