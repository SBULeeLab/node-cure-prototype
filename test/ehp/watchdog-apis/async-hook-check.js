var asyncHooks = require('async_hooks');
var fs = require('fs');

var init = function(asyncId, type, triggerAsyncId, resource) {
	fs.writeSync(2, `init: asyncId ${asyncId} type ${type} triggerAsyncId ${triggerAsyncId}\n`);
};

var before = function(asyncId) {
	fs.writeSync(2, `JS: before: asyncId ${asyncId}\n`);
	process._watchdogStart(asyncId);
};

var after = function(asyncId) {
	fs.writeSync(2, `JS: after: asyncId ${asyncId}\n`);
	process._watchdogStop(asyncId);
};

var destroy = function(asyncId) {
};

var hooks = {
  init: init,
  before: before, 
  after: after,
  destroy: destroy
}

var asyncHook = asyncHooks.createHook(hooks)
asyncHook.enable();

setTimeout(() => {
	console.log('JS: Hello from timeout');
}, 500);

fs.readdir('/tmp', (err, files) => {
	console.log('JS: Hello from readdir');
});
