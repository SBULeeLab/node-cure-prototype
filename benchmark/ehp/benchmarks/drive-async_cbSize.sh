#!/usr/bin/env bash

export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

export ORIGINAL_NODE=nodes/original_node/node
export WATCHDOG_NODE=nodes/watchdog_node/node
export SCRIPT=benchmarks/async_cbSize.js

echo "################"
N_CBS=250000
N_LOOPS=10000
echo "N_CBS $N_CBS N_LOOPS $N_LOOPS"
# Baseline
echo "Baseline"
time $ORIGINAL_NODE $SCRIPT $N_CBS $N_LOOPS
# Lazy
echo "Lazy"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS
# Precise
echo "Precise"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=precise;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS

echo "################"
N_CBS=500000
N_LOOPS=1000
echo "N_CBS $N_CBS N_LOOPS $N_LOOPS"
# Baseline
echo "Baseline"
time $ORIGINAL_NODE $SCRIPT $N_CBS $N_LOOPS
# Lazy
echo "Lazy"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS
# Precise
echo "Precise"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=precise;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS

echo "################"
N_CBS=1000000
N_LOOPS=500
echo "N_CBS $N_CBS N_LOOPS $N_LOOPS"
# Baseline
echo "Baseline"
time $ORIGINAL_NODE $SCRIPT $N_CBS $N_LOOPS
# Lazy
echo "Lazy"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS
# Precise
echo "Precise"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=precise;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS

echo "################"
N_CBS=1000000
N_LOOPS=100
echo "N_CBS $N_CBS N_LOOPS $N_LOOPS"
# Baseline
echo "Baseline"
time $ORIGINAL_NODE $SCRIPT $N_CBS $N_LOOPS
# Lazy
echo "Lazy"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS
# Precise
echo "Precise"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=precise;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS

echo "################"
N_CBS=3000000
N_LOOPS=1
echo "N_CBS $N_CBS N_LOOPS $N_LOOPS"
# Baseline
echo "Baseline"
time $ORIGINAL_NODE $SCRIPT $N_CBS $N_LOOPS
# Lazy
echo "Lazy"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS
# Precise
echo "Precise"
export NODECURE_TIMEOUT_WATCHDOG_TYPE=precise;
time $WATCHDOG_NODE $SCRIPT $N_CBS $N_LOOPS
