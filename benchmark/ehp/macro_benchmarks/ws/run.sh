export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1



watchdog_node=${1}
echo "watchdog node path is ${1}"
original_node=${2}
echo "original node path is ${2}"


for i in $(seq 1 25)
do	
	export NODECURE_TIMEOUT_WATCHDOG_TYPE=precise
	echo $(./run_one.sh "${watchdog_node}") >> watchdog_precise.result
done

for i in $(seq 1 25)
do	
	export NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy
	echo $(./run_one.sh "${watchdog_node}") >> watchdog_lazy.result
done

for i in $(seq 1 25)
do
	echo $(./run_one.sh "${original_node}") >> original.result
done
