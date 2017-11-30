export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

trap "kill 0" SIGINT
cd acmeair-nodejs
${1} app.js > node.output.acmeair&
pid=$!
cd -
sleep 2
curl localhost:3000/rest/api/loader/load?numCustomers=10000 &> /dev/null
cd acmeair-driver
timeout 600 apache-jmeter-3.3/bin/jmeter -DusePureIDs=true -n -t acmeair-jmeter/scripts/AcmeAir.jmx -j AcmeAir1.log -l AcmeAir1.jtl | grep summary

kill ${pid}
mongo acmeair --eval 'db.dropDatabase();' &> /dev/null
