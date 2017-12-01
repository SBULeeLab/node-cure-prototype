console.log('JS: _watchdogStart');
process._watchdogStart(1);

console.log('JS: _watchdogStop');
process._watchdogStop(1);

console.log('JS: watchdog hooks are registered OK');
console.log('JS: Technically this is a way for an application to circumvent our defense, but only because our implementation doesn\'t prevent random people from calling these APIs');
