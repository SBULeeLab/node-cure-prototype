#!/usr/bin/env bash
export NODECURE_THREADPOOL_TIMEOUT_MS=9999999999
export NODECURE_NODE_TIMEOUT_MS=999999999
export NODECURE_SILENT=1
export NODECURE_ASYNC_HOOKS=1

export PATH="$(dirname ${1}):${PATH}"

time (webtorrent/node_modules/tape/bin/tape webtorrent/test/client-add-duplicate-trackers.js webtorrent/test/client-add.js webtorrent/test/client-destroy.js webtorrent/test/client-remove.js webtorrent/test/client-seed.js webtorrent/test/duplicate.js webtorrent/test/rarity-map.js webtorrent/test/swarm.js webtorrent/test/torrent-destroy.js webtorrent/test/node/basic.js webtorrent/test/node/blocklist-dht.js webtorrent/test/node/blocklist.js webtorrent/test/node/blocklist-tracker.js webtorrent/test/node/download-dht-magnet.js webtorrent/test/node/download-dht-torrent.js webtorrent/test/node/download-metadata.js webtorrent/test/node/download-private-dht.js webtorrent/test/node/download-webseed-magnet.js webtorrent/test/node/download-webseed-torrent.js webtorrent/test/node/extensions.js webtorrent/test/node/metadata.js webtorrent/test/node/seed-stream.js webtorrent/test/node/seed-while-download.js webtorrent/test/node/server.js webtorrent/test/node/swarm-basic.js webtorrent/test/node/swarm-reconnect.js webtorrent/test/node/swarm-timeout.js > /dev/null) 2>&1
