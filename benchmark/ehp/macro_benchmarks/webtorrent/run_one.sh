export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

export PATH="$(dirname ${1}):${PATH}"

time (webtorrent/node_modules/tape/bin/tape webtorrent/test/*.js webtorrent/test/node/*.js > /dev/null) 2>&1
