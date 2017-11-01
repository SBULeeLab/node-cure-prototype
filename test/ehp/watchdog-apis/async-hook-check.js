var asyncHooks = require('async_hooks');
var fs = require('fs');

var init = function(asyncId, type, triggerAsyncId, resource) {
	fs.writeSync(2, `init: asyncId ${asyncId} type ${type} triggerAsyncId ${triggerAsyncId}\n`);
};

var before = function(asyncId) {
	process._watchdogStart();
};

var after = function(asyncId) {
	process._watchdogStop();
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
	console.log('Hello from timeout');
}, 500);
