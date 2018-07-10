# node-cure

This is a fork of [Node.js](https://github.com/nodejs/node) implementing the research prototype for the paper [A Sense of Time for JavaScript and Node.js: First-Class Timeouts as a Cure for Event Handler Poisoning](http://people.cs.vt.edu/~davisjam/downloads/publications/DavisWilliamsonLee-SenseOfTime-USENIXSecurity18.pdf) by James C. Davis (@davisjam), Eric R. Williamson (@ewmson), and Dongyoon Lee (@dylosy), all of Virginia Tech.

The prototype is implemented for Linux.

# What does it do?

This prototype makes it relatively difficult to [block the Node.js event loop (or the worker pool)](https://nodejs.org/en/docs/guides/dont-block-the-event-loop/).
Since Node.js uses a single-threaded *Event Loop* and a small, fixed-size *Worker Pool*, a blocked Event Loop or Worker Pool on a back-end Node.js server will deny service (DoS) to your clients.

If the prototype observes that your Event Loop or Worker Pool is blocked, it will emit a *TimeoutError* to your application.
This TimeoutError will be thrown as an exception in synchronous code (e.g. a `while(1){}` loop) and delivered to the callback of a blocked Worker Pool task (e.g. an `fs.read("/dev/random", ..., cb)`).

## Details

The prototype achieves this for the Event Loop by deploying `async-hooks` callbacks to track how long a callback takes.
If a callback takes too long, a TimeoutWatchdog asks V8 to raise a TimeoutError, causing an exception to be thrown in JavaScript-land.

The prototype achieves this for the Worker Pool by adding Manager threads to the libuv threadpool.
If a Manager sees one of the Workers taking too long, it kills it and starts a fresh Worker.
In this case, a TimeoutError will be propagated through the Node.js C++ bindings and delivered to the Event Loop for handling by whoever submitted the long-running job.
We take some care with Tasks that [cannot be safely killed](https://www.gnu.org/software/libc/manual/html_node/POSIX-Safety-Concepts.html).

The prototype cannot help with blocked code in a user-defined C++ binding.
There are relatively few `npm` modules that fit this description. See section 6.3 of the paper for details.

Our changes span V8, libuv, and the Node.js core libraries.
We describe them in section 5 of the [paper](http://people.cs.vt.edu/~davisjam/downloads/publications/DavisWilliamsonLee-SenseOfTime-USENIXSecurity18.pdf).
For specific changes, consult `git log`.

# How do I control the prototype's behavior?

Timeouts and other behaviors are specified by environment variables.

- The environment variable `NODECURE_NODE_TIMEOUT_MS` specifies the timeout (integer, in ms) for long-running Callbacks on the Event Loop.
- The environment variable `NODECURE_THREADPOOL_TIMEOUT_MS` specifies the timeout (integer, in ms) for long-running Tasks in the Worker Pool.
- The environment variable `NODECURE_TIMEOUT_WATCHDOG_TYPE` names the type of TimeoutWatchdog (give "precise" or "lazy"). See section 5.5 of the paper for details.
- The environment variable `NODECURE_ASYNC_HOOKS` enables the asynchronous hooks for the TimeoutWatchdog. Set the value to "1".

# Tests

The test suite mentioned in section 6.1 of the paper is in `test/ehp/unit`.

# Benchmarks

- The simple server described in Figure 2 and benchmarked in Figure 3 is in `test/ehp/app`.
- The micro-benchmarks described in section 6.2 of the paper are in `benchmark/ehp/benchmarks`.
- The macro-benchmarks used to generate Table 3 are in `benchmark/ehp/macro_benchmarks`. See the [README](benchmark/ehp/README) for setup details. @ewmson performed these experiments.
