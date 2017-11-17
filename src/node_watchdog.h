// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_NODE_WATCHDOG_H_
#define SRC_NODE_WATCHDOG_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "v8.h"
#include "uv.h"
#include "node_mutex.h"
#include <stack>
#include <vector>

#ifdef __POSIX__
#include <pthread.h>
#endif

namespace node {

/**
 * Watchdog offers an asynchronous way to wait for a timeout and then execute a CB.
 * The Watchdog maintains a separate thread that monitors the timeout.
 * If the timeout expires, its timeout handler will be called.
 * If you destroy the Watchdog before the timer expires, its abort handler will be called.
 * Either the abort handler or the timeout handler will be called, but not both.
 * If you try to destroy concurrently with the expiration of the timer, it's a race, but only one handler will be called.
 *
 * The Watchdog paradigm is this:
 *   Create a Watchdog with a time limit (ms) and handlers for abort and for timeout.
 *   Do work on your thread.
 *     - Make sure you check to see if the Watchdog has expired.
 *       For example, the Watchdog timeout might signal your thread that it should clean up.
 *   On completion, destroy the Watchdog -- the Watchdog will run the abort handler.
 */
class Watchdog {
 public:
  typedef void (*WatchdogFunc)(void *data);

  explicit Watchdog(uint64_t ms,
										WatchdogFunc aborted_cb, // Called on destruction if the timer did not go off.
										WatchdogFunc timeout_cb, // Called on expiration of the timer.
										void *data = nullptr // Passed to the WatchdogFunc's.
										);
  ~Watchdog();

 private:
  static void Run(void* arg);
  static void Async(uv_async_t* async);
  static void Timer(uv_timer_t* timer);

  v8::Isolate* isolate_;
  uv_thread_t thread_;
  uv_loop_t* loop_;
  uv_async_t async_;
  uv_timer_t timer_;

	/* Make sure we only call one of aborted_cb_ and timeout_cb_.
	 * No lock required because these are tested in Async and Timer, which both run on the Watchdog thread (atomicity guarantee). */
	bool timed_out_;
	bool aborted_;

	WatchdogFunc aborted_cb_;
	WatchdogFunc timeout_cb_;
	void *data_;
};

/**
 * State diagram:                   
 *                                AfterHook called from an async-hook
 *                               -------------------
 *      Executing startup JS     |                 |            Timer expires. Call Isolate::Timeout
 *  init --------------------> idle --------> countdown ------------>  Timeout 
 *                                BeforeHook           \            |
 *                            called from an async-hook ------------|
 *                                                         Self-reset to continue aborting.
 *                                                         Ultimately, we can't protect a script that keeps ignoring
 *                                                         the errors we throw, e.g.
 *                                                           while(1) { try{ while(1); } catch(){} }
 *
 */
class TimeoutWatchdog { // TimeoutWatchdog (TW). Timeouts in ms granularity.
	public:
		TimeoutWatchdog(v8::Isolate *isolate, uint64_t timeout_ms);
		~TimeoutWatchdog();

		v8::Isolate* isolate() { return isolate_; }

		/* Event loop APIs. */

		/**
		 * Event Loop thread starts a new async operation.
		 * Call this from an async-hook and provide the async_id.
		 * Call from every "before" hook.
		 */
	  void BeforeHook (long async_id);

		/**
		 * Event Loop thread tells TW that the async operation for this async_id has completed.
		 * Should be LIFO with BeforeHook.
		 * Call from every "after" hook.
		 */
		void AfterHook (long async_id);

		/**
     * JS code calls into a C++ binding that's TW-aware.
		 * The C++ binding tells TW that it will manage the timer for now.
		 *
		 * Returns time left on the countdown.
		 * Until Unleash is called, no timeout will be thrown by this TW.
		 */
		uint64_t Leash (void);

    /**
		 * The C++ binding has returned. if(threw), it threw a timeout exception.
		 * TW resumes the countdown as appropriate.
		 */
		void Unleash (bool threw);

		/* TW thread APIs. */

		/**
		 * Entry point for the thread that occupies this TW.
		 * The TW responds to the public APIs.
		 * The possessing thread is joined in the destructor.
		 */
		static void Run (void *arg);

		static inline uint64_t NowMs(void) { return uv_hrtime() / 1000000; } /* 10e9 / 10e6 = 10e3 */

	private:
		/* Helpers for Event Loop. */
		void _SignalWatchdog();

		/* Helpers for TW thread. */

		/**
		 * Start a timer expiring in expiry_ms.
		 */
		void _StartTimer(uint64_t expiry_ms);

		/**
		 * Stop the current timer.
		 */
		void _StopTimer();

		/**
		 * Start a new timer epoch.
		 */
		void _StartEpoch(void);

		/* Entrance points for TW thread. */

		/**
		 * Communication from the outside world.
		 */
		static void Async(uv_async_t* async);

		/**
		 * A timeout expired.
		 */
		static void Timer(uv_timer_t* timer);

		/* Class members. */

		/* Constants. */
		uint64_t timeout_ms_; // How long each BeforeHook has before we time it out.
		v8::Isolate *isolate_; // The associated isolate.
		uv_thread_t thread_; // The TW thread.

		/* Managing the TW thread. */
		uv_loop_t *loop_;  // TW thread uses a uv_loop_t to manage an async (for async-hook communication) and a timer (for timeouts).
		uv_async_t async_; // uv_async_send when: (1) the async_ids stack is newly empty or newly non-empty, (2) leash change, (3) stopping.
		uv_timer_t timer_; // If it goes off, trigger a timeout if the state is the same as when we started the timer.

		/* Mutable fields. The TW thread and the Event Loop thread communicate through these since TW thread responds asynchronously. */
	  uv_mutex_t lock_; // Protects all this stuff.

		/* BeforeHook, AfterHook */
		std::stack<long> pending_async_ids_; // Nested async hooks are called in a stack.
		long stack_num_; // Number of stacks we have seen -- when stack goes from empty to non-empty, +1.
		uint64_t time_at_stack_change_ms_;

		/* Leash, Unleash. */
		bool leashed_; // True if we shouldn't throw timeouts.
		uint64_t time_at_leash_ms_;
		long stack_num_at_leash_; // Reset after handling the corresponding Unleash.
		long stack_num_at_unleash_; // Reset after handling.
		bool threw_while_leashed_;

		/* _StartTimer. */
		uint64_t epoch_start_time_ms_; // The time at which we first started the
		                               // current timer sequence for this stack_num_.
		                               // i.e. this is the time at which we started a timer for timeout_ms_.
																	 // On timer expiry we begin a new epoch.
		long stack_num_at_epoch_start_;

		/* ~TimeoutWatchdog. */
		bool stopping_; // Signal TW to clean up.
		uv_sem_t stopped_; // TW posts when it receives the final Async
};

class SigintWatchdog {
 public:
  explicit SigintWatchdog(v8::Isolate* isolate,
                          bool* received_signal = nullptr);
  ~SigintWatchdog();
  v8::Isolate* isolate() { return isolate_; }
  void HandleSigint();

 private:
  v8::Isolate* isolate_;
  bool* received_signal_;
};

class SigintWatchdogHelper {
 public:
  static SigintWatchdogHelper* GetInstance() { return &instance; }
  void Register(SigintWatchdog* watchdog);
  void Unregister(SigintWatchdog* watchdog);
  bool HasPendingSignal();

  int Start();
  bool Stop();

 private:
  SigintWatchdogHelper();
  ~SigintWatchdogHelper();

  static bool InformWatchdogsAboutSignal();
  static SigintWatchdogHelper instance;

  int start_stop_count_;

  Mutex mutex_;
  Mutex list_mutex_;
  std::vector<SigintWatchdog*> watchdogs_;
  bool has_pending_signal_;

#ifdef __POSIX__
  pthread_t thread_;
  uv_sem_t sem_;
  bool has_running_thread_;
  bool stopping_;

  static void* RunSigintWatchdog(void* arg);
  static void HandleSignal(int signum);
#else
  bool watchdog_disabled_;
  static BOOL WINAPI WinCtrlCHandlerRoutine(DWORD dwCtrlType);
#endif
};

void node_log (int verbosity, const char *format, ... );

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_WATCHDOG_H_
