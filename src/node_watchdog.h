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
 * If the thing you were waiting for completes before the timeout expires, destroying the Watchdog
 *   will prevent the timeout CB from running.
 * You can register one CB for the case of "we aborted before timeout",
 * and another CB for the case of "we timed out".
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

	bool timed_out_;

	WatchdogFunc aborted_cb_;
	WatchdogFunc timeout_cb_;
	void *data_;
};

/**
 * State diagram:                   
 *                                DisableCountdown called from an async-hook
 *                               -------------------
 *      Executing startup JS     |                 |     Timer expires. Call Isolate::Timeout
 *  init --------------------> idle --------> countdown ------------>  Timeout 
 *                                StartCountdown       \                |
 *                            called from an async-hook ----------------|
 *                                                         Self-reset to continue aborting.
 *                                                         Ultimately, we can't protect a script that keeps ignoring
 *                                                         the errors we throw, e.g.
 *                                                           while(1) { try{ while(1); } catch(){} }
 *
 */
class TimeoutWatchdog { // TimeoutWatchdog (TW). Timeouts in ms granularity.
	public:
		TimeoutWatchdog(v8::Isolate *isolate, long timeout_ms);
		~TimeoutWatchdog();

		v8::Isolate* isolate() { return isolate_; }

		/* Event loop APIs. */

		/**
		 * Event Loop thread tells TW to start a countdown.
		 * Call this from an async-hook and provide the async_id.
		 * This is nesting-safe. Call from every "before" hook.
		 */
	  void StartCountdown (long async_id);

		/**
		 * Event Loop thread tells TW to disable the countdown for this async_id.
		 * This is nesting-safe. Call from every "after" hook.
		 */
		void DisableCountdown (long async_id);

#if 0
		/**
		 * If there is an active countdown, report how long it was for.
		 * Otherwise returns -1.
		 */
		long GetCountdownTimeout ();

		/* If there is an active countdown, report the time remaining.
		 * Otherwise returns -1.
		 */
		long GetCountdownRemaining ();
#endif

		/* Returns true if there is an active countdown and it has expired. */
		bool HasTimerExpired ();

		/* TW thread APIs. */

		/**
		 * Entry point for the thread that occupies this TW.
		 * The TW responds to the public APIs.
		 * The possessing thread is joined in the destructor.
		 */
		static void Run (void *arg);

	private:
		/* Helpers for Event Loop APIs. */
		// XXX

		/* Helpers for TW thread. */

		/**
		 * A new call stack has begun!
		 *  - Start a timer.
		 *  - Mark TW active.
		 */
		void _StartTimer();

		/* Entrance points for TW thread. */

		/**
		 * The queue was updated by AddAsyncId.
		 */
		static void Async(uv_async_t* async);

		/**
		 * A timeout expired.
		 */
		static void Timer(uv_timer_t* timer);

		/* Class members. */

		long timeout_ms_; // How long each StartCountdown has before we time it out.
		v8::Isolate *isolate_; // The associated isolate.
		uv_thread_t thread_; // The TW thread.

		uv_loop_t *loop_; // TW thread uses a uv_loop_t to manage an async (for async-hook communication) and a timer (for timeouts).
		uv_async_t async_; // uv_async_send when the async_ids stack is newly empty or newly non-empty.
		uv_timer_t timer_; // If it goes off, trigger a timeout.

	  uv_mutex_t lock_; // Protects all this stuff.
		std::stack<long> pending_async_ids_; // Nested async hooks are called in a stack.
		bool active_; // True if there was an active call stack the last time we went through Async().
		bool timed_out_; // True if we timed out on this call stack.
		bool stopping_; // Signal TW to clean up.
		uv_sem_t stopped_; // TW posts when it receives the final Async

		uint64_t time_of_last_timeout_ns_;
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

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_WATCHDOG_H_
