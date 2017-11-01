console.log('JS: _watchdogStart');
process._watchdogStart();

console.log('JS: _watchdogExpired');
var expired = process._watchdogExpired();
console.log('expired: ' + expired);

console.log('JS: _watchdogStop');
process._watchdogStop();
