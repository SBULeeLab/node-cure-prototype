#!/usr/bin/env node
/*
 * Load the redos.js file and execute it in a VM context.
 *
 * Behavior: It times out promptly.
 * Conclusion: Isolate::TerminateExecution will terminate a runaway regexp.
 */

var vm = require('vm');
var fs = require('fs');

var ctxt = {};
vm.createContext(ctxt);

var code = fs.readFileSync('redos.js', 'utf8');
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
