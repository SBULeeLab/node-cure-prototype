#!/bin/bash

# setup the working directories
$(git clone .. original_node && cd original_node && git checkout "dc6bbb4" && ./configure && make -j $(nproc) && cd -)
$(git clone .. watchdog_node && cd watchdog_node && git checkout NODECURE_SILENT && ./configure && make -j $(nproc) && cd -)
$(cd benchmarks && head -c 500M /dev/urandom >random_file)

echo "created node projects, now to run benchmarks"

mkdir results
shopt -s nullglob
for file in benchmarks/*.js
do
	
  resultfile=results/"$(basename "${file}" .js)".result
	echo "benchmarking ${file} see the results in ${resultfile}"
	./bench.sh "${file}" &> "${resultfile}"
done



echo "running benchmarks witha single cpu"

mkdir results_single_cpu
shopt -s nullglob
for file in benchmarks/*.js
do
  resultfile=results_single_cpu/"$(basename "${file}" .js)".result
        echo "benchmarking ${file} with a single cpu see the results in ${resultfile}"
        ./bench_one_cpu.sh "${file}" &> "${resultfile}"
done
