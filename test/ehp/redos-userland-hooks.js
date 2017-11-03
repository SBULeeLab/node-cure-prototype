/**
 * redos from a setTimeout CB.
 * With Node-Cure, it throws a Timeout which we catch and log.
 */

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

/////////////////////////////////////
setTimeout(() => {
	console.log('JS: Entering timeout');
	var len = 30; // 31: 25 seconds
	var str = '';
	for (var i = 0; i < len; i++)
		str += 'a';
	str += '!'; // mismatch
	var re = /(a+)+$/;

	var ret = '';
	try {
		redos(str, re);
	} catch (e) {
		console.log('JS: Caught exception:');
		console.log(e);
		ret = e;
	}
}, 1);

function redos (str, re) {
		console.log('JS: redos: Performing match with str len ' + str.length);
	if (str.match(re))
		console.log('JS: Match');
	else 
		console.log('JS: Mismatch');
}
