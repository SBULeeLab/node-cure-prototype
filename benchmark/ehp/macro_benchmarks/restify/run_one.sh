export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

o=$(${1} node-restify/benchmark/index.js)

cat node-restify/benchmark/results/response-json-head.json
echo ""
cat node-restify/benchmark/results/response-text-head.json 
