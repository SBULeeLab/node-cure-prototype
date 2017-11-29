export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

export PATH="$(dirname ${1}):${PATH}"
export BENCHMARK=true

sails/node_modules/mocha/bin/mocha sails/test/benchmarks/ | grep runs\ sampled 
