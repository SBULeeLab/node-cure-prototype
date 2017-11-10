var asyncHooks = require('async_hooks');
var fs = require('fs');

var stack = [];

var init = function(asyncId, type, triggerAsyncId, resource) {
	fs.writeSync(2, `init: asyncId ${asyncId} type ${type} triggerAsyncId ${triggerAsyncId}\n`);
};

var before = function(asyncId) {
	fs.writeSync(2, `JS: HOOK: before: asyncId ${asyncId}\n`);
	process._watchdogStart(asyncId);
	stack.push(asyncId);
	fs.writeSync(2, `JS: HOOK: before: stack has ${stack.length} elements at return\n`);
};

var after = function(asyncId) {
	fs.writeSync(2, `JS: HOOK: after: asyncId ${asyncId}\n`);
	fs.writeSync(2, `JS: HOOK: after: stack has ${stack.length} elements at entry\n`);
	process._watchdogStop(asyncId);
	stack.pop();
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

for (var i = 0; i < 100; i++) {
	setTimeout(() => {
		console.log('JS: Hello from timeout');
	}, 1);
}

for (var i = 0; i < 100; i++) {
	fs.open('/tmp/raw.dat', 'r', () => {
		console.log('JS: Hello from open');
	});
}

setTimeout(() => {
	console.log('JS: Hello from timeout');
}, 500);

fs.readdir('/tmp', (err, files) => {
	console.log('JS: Hello from readdir');
});
