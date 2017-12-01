node-oniguruma defense: See node-cure/patches

simpleServer.js:

  Node.CURE

    export NODECURE_THREADPOOL_TIMEOUT_MS=1000 NODECURE_NODE_TIMEOUT_MS=100 NODECURE_ASYNC_HOOKS=1 NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy NODECURE_SILENT=1; for i in `seq 1 5`; do echo; echo; echo; echo NEW SERVER; echo; echo; echo; timeout 10 ../../../node simpleServer.js 3000 > /tmp/results-NodeCure-REDOS-$i.dat 2>/tmp/err; echo; echo SERVER DONE; sleep 3; done

    export NODECURE_THREADPOOL_TIMEOUT_MS=1000 NODECURE_NODE_TIMEOUT_MS=100 NODECURE_ASYNC_HOOKS=1 NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy NODECURE_SILENT=1; for i in `seq 1 5`; do echo; echo; echo; echo NEW SERVER; echo; echo; echo; timeout 10 ../../../node simpleServer.js 3000 > /tmp/results-NodeCure-readDOS-$i.dat 2>/tmp/err; echo; echo '  SERVER DONE'; sleep 1; done

    export NODECURE_THREADPOOL_TIMEOUT_MS=1000 NODECURE_NODE_TIMEOUT_MS=100 NODECURE_ASYNC_HOOKS=1 NODECURE_TIMEOUT_WATCHDOG_TYPE=lazy NODECURE_SILENT=1; for i in `seq 1 5`; do echo; echo; echo; echo NEW SERVER; echo; echo; echo; timeout 10 ../../../node simpleServer.js 3000 > /tmp/results-NodeCure-allOut-$i.dat 2>/tmp/err; echo; echo '  SERVER DONE'; sleep 1; done

  Baseline

     for i in `seq 1 5`; do echo; echo; echo; echo NEW SERVER; echo; echo; echo; timeout 10 ../../../benchmark/ehp/nodes/original_node/node simpleServer.js 3000 > /tmp/results-Baseline-REDOS-$i.dat 2>/tmp/err; echo; echo '  SERVER DONE'; sleep 1; done

     for i in `seq 1 5`; do echo; echo; echo; echo NEW SERVER; echo; echo; echo; timeout 5 ../../../benchmark/ehp/nodes/original_node/node simpleServer.js 3000 > /tmp/results-Baseline-readDOS-$i.dat 2>/tmp/err; echo; echo '  SERVER DONE'; sleep 1; done
