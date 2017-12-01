// NODECURE_NODE_TIMEOUT_MS=10 ../../../../../node readdirSync.js
// This is implemented in libuv as a call to scandir, which should time out.

// NODECURE_NODE_TIMEOUT_MS=100 ../../../../../node readdirSync.js

/* TODO: Timeout sometimes leads to a V8 crash. This is unique to readdirSync.

   See 57e735d867, 910956eed1b.
   Worth revisiting this to double-check validity and reasoning, but
   these commits eliminated the crash.

	(00:53:15) jamie@jamie-Lenovo-K450e ~/Desktop/ehp-attacks/node-cure/test/ehp/unit/framework/fs $ NODECURE_NODE_TIMEOUT_MS=50 ../../../../../node readdirSync.js
	FATAL ERROR: v8::ToLocalChecked Empty MaybeLocal.
	 1: node::Abort() [../../../../../node]
	 2: 0x127fffc [../../../../../node]
	 3: v8::Utils::ReportApiFailure(char const*, char const*) [../../../../../node]
	 4: 0x12bddf5 [../../../../../node]
	 5: v8::internal::FunctionCallbackArguments::Call(void (*)(v8::FunctionCallbackInfo<v8::Value> const&)) [../../../../../node]
	 6: 0xb258f7 [../../../../../node]
	 7: v8::internal::Builtin_HandleApiCall(int, v8::internal::Object**, v8::internal::Isolate*) [../../../../../node]
	 8: 0x19b49a28463d
	Aborted (core dumped)

*/

var fs = require('fs');

setTimeout(() => {
  try {
    for (var i = 0; i < 100; i++)
      var files = fs.readdirSync('/tmp');
  }
  catch (e) {
    console.log('Threw:');
    console.log(e);
  }
});
