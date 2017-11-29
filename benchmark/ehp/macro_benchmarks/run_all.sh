
watchdog_node=$(readlink -e "../nodes/watchdog_node/node")
original_node=$(readlink -e "../nodes/original_node/node")
echo "watchdog_node is ${watchdog_node}"
echo "original_node is ${original_node}"
for test in express koa lokijs restify ws sails
do
  echo "running benchmarks for ${test}"
  echo $(cd ${test}; ./run.sh "${watchdog_node}" "${original_node}")
done
