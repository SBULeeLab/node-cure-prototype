const util = require('util');
const vm = require('vm');

const sandbox = { globalVar: 1 };
vm.createContext(sandbox);

for (let i = 0; i < 10; ++i) {
	  var ret = vm.runInContext('globalVar *= 2;', sandbox);
		console.log('ret: ' + ret);
}
console.log(util.inspect(sandbox));
console.log('global: ' + sandbox.globalVar);
