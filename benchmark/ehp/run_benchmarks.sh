#!/bin/bash

export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

env | grep NODECURE

# setup the working directories
$(cd benchmarks && head -c 100M /dev/urandom >random_file)

echo "created node projects, now to run benchmarks"

mkdir -p results
shopt -s nullglob
for file in benchmarks/*.js
do
	
  resultfile=results/"$(basename "${file}" .js)".result
	echo "benchmarking ${file} see the results in ${resultfile}"
	./bench.sh "${file}" &> "${resultfile}"
done



echo "running benchmarks with a single cpu"

mkdir -p results_single_cpu
shopt -s nullglob
for file in benchmarks/*.js
do
  resultfile=results_single_cpu/"$(basename "${file}" .js)".result
        echo "benchmarking ${file} with a single cpu see the results in ${resultfile}"
        ./bench_one_cpu.sh "${file}" &> "${resultfile}"
done
