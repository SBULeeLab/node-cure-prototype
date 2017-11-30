#!/usr/bin/env bash
export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

export PATH="$(dirname ${1}):${PATH}"

time ( three.js/node_modules/qunitjs/bin/qunit three.js/test/unit/three.source.unit.js >/dev/null 2>&1 ) 2>&1
