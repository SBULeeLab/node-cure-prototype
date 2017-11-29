export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

cd LokiJS/benchmark/ ; ${1} benchmark.js; ${1} benchmark_binary.js
