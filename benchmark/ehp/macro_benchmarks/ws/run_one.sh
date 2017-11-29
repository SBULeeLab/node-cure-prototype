export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1



${1} ws/bench/sender.benchmark.js | grep runs\ sampled 
${1} ws/bench/parser.benchmark.js | grep runs\ sampled 
