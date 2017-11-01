#!/usr/bin/env node
/*
 * Load the iodos.js file and execute it in a VM context.
 * Note that you have to expose require and Buffer to get this to work.
 * It seems like the goal of VM is to sandbox pure JS, to make sure it doesn't mess with global variables.
 *   cf. https://groups.google.com/forum/#!topic/nodejs/vFkaH8gouDU
 *
 * Behavior: IODOS still blocks.
 * Conclusion: Isolate::TerminateExecution cannot abort a blocked syscall.
 */

var vm = require('vm');
var fs = require('fs');

var ctxt = { require: require, Buffer: Buffer };
vm.createContext(ctxt);

var code = fs.readFileSync('iodos.js', 'utf8');
var lines = code.split('\n');
if (lines[0].match(/#!/))
	lines.shift();
code = lines.join('\n');

var start = process.hrtime();
try {
	console.log('Running code in a context');
	console.log(code);
	vm.runInContext(code, ctxt, { timeout: 1000 });
} catch (e) {
	console.log('vm threw after ' + process.hrtime(start));
	console.log(e);
}
