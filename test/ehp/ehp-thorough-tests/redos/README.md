These are all the JS operations that use regexps.
	- RegExp operations
	- String operations that take RegExps

All of them use RawMatch() under the hood, so all of them are protected by NodeCure using V8_INTERPRETED_IRREGEXP.

Note that Atom is unprotected, but Atom is strstr() which is safe provided the inputs aren't too big.
That's ensured by our application model.
