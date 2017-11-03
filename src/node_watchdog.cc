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

#include "node_watchdog.h"
#include "node_internals.h"
#include <algorithm>

namespace node {

Watchdog::Watchdog(uint64_t ms, WatchdogFunc aborted_cb, WatchdogFunc timeout_cb, void *data)
    : aborted_cb_(aborted_cb), timeout_cb_(timeout_cb), data_(data), timed_out_(false) {

  int rc;
  loop_ = new uv_loop_t;
  CHECK(loop_);
  rc = uv_loop_init(loop_); // NB This creates worker pool that we don't need.
  if (rc != 0) {
    FatalError("node::Watchdog::Watchdog()",
               "Failed to initialize uv loop.");
  }

  rc = uv_async_init(loop_, &async_, &Watchdog::Async);
  CHECK_EQ(0, rc);

  rc = uv_timer_init(loop_, &timer_);
  CHECK_EQ(0, rc);

  rc = uv_timer_start(&timer_, &Watchdog::Timer, ms, 0);
  CHECK_EQ(0, rc);

  rc = uv_thread_create(&thread_, &Watchdog::Run, this);
  CHECK_EQ(0, rc);
}


Watchdog::~Watchdog() {
  uv_async_send(&async_);
  uv_thread_join(&thread_);

  uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);

  // UV_RUN_DEFAULT so that libuv has a chance to clean up.
  uv_run(loop_, UV_RUN_DEFAULT);

  int rc = uv_loop_close(loop_);
  CHECK_EQ(0, rc);
  delete loop_;
  loop_ = nullptr;
}


void Watchdog::Run(void* arg) {
  Watchdog* wd = static_cast<Watchdog*>(arg);

  // UV_RUN_DEFAULT the loop will be stopped either by the async or the
  // timer handle.
  uv_run(wd->loop_, UV_RUN_DEFAULT);

  // Loop ref count reaches zero when both handles are closed.
  // Close the timer handle on this side and let ~Watchdog() close async_
  uv_close(reinterpret_cast<uv_handle_t*>(&wd->timer_), nullptr);
}


void Watchdog::Async(uv_async_t* async) {
  Watchdog* w = ContainerOf(&Watchdog::async_, async);
	uv_stop(w->loop_);

	if (!w->timed_out_ && w->aborted_cb_ != NULL)
		w->aborted_cb_(w->data_);
}


void Watchdog::Timer(uv_timer_t* timer) {
  Watchdog* w = ContainerOf(&Watchdog::timer_, timer);
  w->timed_out_ = true;
  uv_stop(w->loop_);

	if (w->timeout_cb_ != NULL)
		w->timeout_cb_(w->data_);
}

TimeoutWatchdog::TimeoutWatchdog(v8::Isolate* isolate, long timeout_ms)
    : isolate_(isolate), timeout_ms_(timeout_ms), active_(false), stopping_(false), time_of_last_timeout_ns_(0) {

  int rc;

	CHECK(pending_async_ids_.empty()); // Call me paranoid...

	rc = uv_mutex_init(&lock_);
	CHECK(rc == 0);

	rc = uv_sem_init(&stopped_, 0);
	CHECK(rc == 0);

  loop_ = new uv_loop_t;
  CHECK(loop_);
  rc = uv_loop_init(loop_); // NB This creates worker pool that we don't need. Same as Watchdog though.
  if (rc != 0) {
    FatalError("node::TimeoutWatchdog::TimeoutWatchdog()",
               "Failed to initialize uv loop.");
  }

  rc = uv_async_init(loop_, &async_, &TimeoutWatchdog::Async);
  CHECK_EQ(0, rc);

  rc = uv_timer_init(loop_, &timer_);
  CHECK_EQ(0, rc);

  rc = uv_thread_create(&thread_, &TimeoutWatchdog::Run, this);
  CHECK_EQ(0, rc);
}

TimeoutWatchdog::~TimeoutWatchdog() {
	/* Clean up the TW thread. */
  dprintf(2, "TimeoutWatchdog::~TimeoutWatchdog: Cleaning up TW thread\n");
	uv_timer_stop(&timer_);

	// Signal TW thread that we're done.
	uv_mutex_lock(&lock_);
		dprintf(2, "TimeoutWatchdog::~TimeoutWatchdog: Signaling TW thread\n");
		stopping_ = true; 
		uv_async_send(&async_);
	uv_mutex_unlock(&lock_);

	dprintf(2, "TimeoutWatchdog::~TimeoutWatchdog: Waiting for TW thread to down the semaphore\n");
	uv_sem_wait(&stopped_);

	// Close handles so loop_ will stop.
  // Loop ref count reaches zero when both handles are closed.
  dprintf(2, "TimeoutWatchdog::~TimeoutWatchdog: Closing handles on loop_\n");
  uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
	uv_timer_stop(&timer_);
  uv_close(reinterpret_cast<uv_handle_t*>(&timer_), nullptr);

  dprintf(2, "TimeoutWatchdog::~TimeoutWatchdog: Joining thread\n");
  uv_thread_join(&thread_);

  // UV_RUN_DEFAULT so that libuv has a chance to clean up.
  dprintf(2, "TimeoutWatchdog::~TimeoutWatchdog: Final uv_run for libuv cleanup\n");
  uv_run(loop_, UV_RUN_DEFAULT);

  int rc = uv_loop_close(loop_);
  CHECK_EQ(0, rc);
  delete loop_;
  loop_ = nullptr;

	uv_mutex_destroy(&lock_);
}

void TimeoutWatchdog::StartCountdown(long async_id) {
	uv_mutex_lock(&lock_);
	CHECK(!stopping_);

	bool was_empty = pending_async_ids_.empty();
	pending_async_ids_.push(async_id);

	// Signal wd thread: change in state.
  if (was_empty)
		uv_async_send(&async_);

	dprintf(2, "TimeoutWatchdog::StartCountdown: was_empty %d\n", was_empty);
	uv_mutex_unlock(&lock_);

	return;
}

void TimeoutWatchdog::DisableCountdown(long async_id) {
	uv_mutex_lock(&lock_);
	CHECK(!stopping_);

	CHECK(!pending_async_ids_.empty());
	pending_async_ids_.pop();
	bool now_empty = pending_async_ids_.empty();

	// Signal wd thread: change in state.
	if (now_empty)
		uv_async_send(&async_);

	dprintf(2, "TimeoutWatchdog::DisableCountdown: now_empty %d\n", now_empty);
	uv_mutex_unlock(&lock_);

	return;
}

void TimeoutWatchdog::Run(void* arg) {
  TimeoutWatchdog* wd = static_cast<TimeoutWatchdog*>(arg);

	dprintf(2, "TimeoutWatchdog::Run: Calling uv_run\n");
  uv_run(wd->loop_, UV_RUN_DEFAULT); // Run until we call uv_stop due to signal from TimeoutWatchdog::~TimeoutWatchdog.
	dprintf(2, "TimeoutWatchdog::Run: returning\n");
}

/**
 * At some point, a StartCountdown found the stack empty or a DisableCountdown emptied the stack.
 * Since uv_async_send can be coalesced, we can't assume anything about the state of the stack now.
 */
void TimeoutWatchdog::Async(uv_async_t* async) {
  TimeoutWatchdog *w = ContainerOf(&TimeoutWatchdog::async_, async);

  dprintf(2, "TimeoutWatchdog::Async: entry\n");
	uv_mutex_lock(&w->lock_);
	bool any_async_ids = !w->pending_async_ids_.empty();
	int rc;

	if (w->stopping_) {
		dprintf(2, "TimeoutWatchdog::Async: stopping_, calling uv_stop\n");
		uv_stop(w->loop_);
		uv_sem_post(&w->stopped_);
		goto UNLOCK_AND_RETURN;
	}

	if (w->active_) {
		dprintf(2, "TimeoutWatchdog::Async: I was active\n");
		// If we were active and someone uv_async_send'd us:
		//   - the stack emptied at some point.
		//   - therefore the current timer should be stopped.
		rc = uv_timer_stop(&w->timer_);
		CHECK_EQ(rc, 0);

		if (any_async_ids) {
			// If the stack is occupied, the 'empty' async send was coalesced with a 'not empty' send.
			// We should start a timer.
			dprintf(2, "TimeoutWatchdog::Async: Stack is occupied, starting the timer\n");
			w->_StartTimer();
		}
		else {
			dprintf(2, "TimeoutWatchdog::Async: Stack is empty, nothing to do\n");
			w->active_ = false;
			w->timed_out_ = false; // When the stack is clear, the timeout has been resolved.
		}
	}
	else {
			dprintf(2, "TimeoutWatchdog::Async: I was not active\n");
		// If we were not active and someone uv_async_send'd us:
		//   - the stack was not empty at some point.
		//   - therefore it may still not be empty
		if (any_async_ids) {
			w->_StartTimer();
		}
		else
			w->timed_out_ = false; // No timeout pending.
	}

UNLOCK_AND_RETURN:
	dprintf(2, "TimeoutWatchdog::Async: active_ %d stopping_ %d\n", w->active_, w->stopping_);
	uv_mutex_unlock(&w->lock_);
	return;
}

bool TimeoutWatchdog::HasTimerExpired () {
	uv_mutex_lock(&lock_);

	bool expired = timed_out_;

	dprintf(2, "TimeoutWatchdog::HasTimerExpired: %d\n", expired);
	uv_mutex_unlock(&lock_);

	return expired;
}

/* Caller should hold lock_. */
void TimeoutWatchdog::_StartTimer() {
	uint64_t now_ms = uv_hrtime() / 1000000;
	dprintf(2, "TimeoutWatchdog::_StartTimer: entry at %lld\n", now_ms);

	uv_timer_stop(&timer_); // Make sure it's stopped. Now sure what happens if I start a started timer, but this is cheap since the timer heap has size <= 1.
	uint64_t repeat = 0; // Don't repeat; libuv's timers go off at timeout_ms_ intervals accounting for time shaving. We restart with full timeout ourselves.
	int rc = uv_timer_start(&timer_, &TimeoutWatchdog::Timer, timeout_ms_, repeat);
	CHECK_EQ(0, rc);

	active_ = true;
}


void TimeoutWatchdog::Timer(uv_timer_t* timer) {
  TimeoutWatchdog* w = ContainerOf(&TimeoutWatchdog::timer_, timer);

	uint64_t last_timeout_ns, now_ns, since_in_ms;

  dprintf(2, "TimeoutWatchdog::Timer: entry at %ld\n", (long) time(NULL));
	uv_mutex_lock(&w->lock_);

	if (w->stopping_) {
			dprintf(2, "TimeoutWatchdog::Timer: stopping_, returning early\n");
			goto UNLOCK_AND_RETURN;
	}

	last_timeout_ns = w->time_of_last_timeout_ns_;
	now_ns = uv_hrtime();
	CHECK(last_timeout_ns < now_ns);
	since_in_ms = (now_ns - last_timeout_ns) / 1000000; // 10e9 / 10e6 = 10e3

	if (0 < last_timeout_ns) { // Check after the first timeout
		dprintf(2, "TimeoutWatchdog::Timer: %lld ms since last timeout (timeout_ms %lld)\n", since_in_ms, w->timeout_ms_);
		CHECK(w->timeout_ms_ <= since_in_ms + 1); // Timeouts should be well spaced. +1 to account for truncating effect of division.
	}

	CHECK(w->active_);
  w->timed_out_ = true;
  dprintf(2, "TimeoutWatchdog::Timer: Calling isolate()->Timeout()\n");
  w->isolate()->Timeout(); // TODO Is it safe to call Timeout if the previous Timeout interrupt has been handled but the Timeout it threw hasn't cleared yet? Need to modify V8-land?

	w->time_of_last_timeout_ns_ = now_ns;
	w->_StartTimer();

UNLOCK_AND_RETURN:
  dprintf(2, "TimeoutWatchdog::Timer: return\n");
	uv_mutex_unlock(&w->lock_);
	return;
}

SigintWatchdog::SigintWatchdog(
  v8::Isolate* isolate, bool* received_signal)
    : isolate_(isolate), received_signal_(received_signal) {
  // Register this watchdog with the global SIGINT/Ctrl+C listener.
  SigintWatchdogHelper::GetInstance()->Register(this);
  // Start the helper thread, if that has not already happened.
  SigintWatchdogHelper::GetInstance()->Start();
}


SigintWatchdog::~SigintWatchdog() {
  SigintWatchdogHelper::GetInstance()->Unregister(this);
  SigintWatchdogHelper::GetInstance()->Stop();
}


void SigintWatchdog::HandleSigint() {
  *received_signal_ = true;
  isolate_->TerminateExecution();
}

#ifdef __POSIX__
void* SigintWatchdogHelper::RunSigintWatchdog(void* arg) {
  // Inside the helper thread.
  bool is_stopping;

  do {
    uv_sem_wait(&instance.sem_);
    is_stopping = InformWatchdogsAboutSignal();
  } while (!is_stopping);

  return nullptr;
}


void SigintWatchdogHelper::HandleSignal(int signum) {
  uv_sem_post(&instance.sem_);
}

#else

// Windows starts a separate thread for executing the handler, so no extra
// helper thread is required.
BOOL WINAPI SigintWatchdogHelper::WinCtrlCHandlerRoutine(DWORD dwCtrlType) {
  if (!instance.watchdog_disabled_ &&
      (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT)) {
    InformWatchdogsAboutSignal();

    // Return true because the signal has been handled.
    return TRUE;
  } else {
    return FALSE;
  }
}
#endif


bool SigintWatchdogHelper::InformWatchdogsAboutSignal() {
  Mutex::ScopedLock list_lock(instance.list_mutex_);

  bool is_stopping = false;
#ifdef __POSIX__
  is_stopping = instance.stopping_;
#endif

  // If there are no listeners and the helper thread has been awoken by a signal
  // (= not when stopping it), indicate that by setting has_pending_signal_.
  if (instance.watchdogs_.empty() && !is_stopping) {
    instance.has_pending_signal_ = true;
  }

  for (auto it : instance.watchdogs_)
    it->HandleSigint();

  return is_stopping;
}


int SigintWatchdogHelper::Start() {
  Mutex::ScopedLock lock(mutex_);

  if (start_stop_count_++ > 0) {
    return 0;
  }

#ifdef __POSIX__
  CHECK_EQ(has_running_thread_, false);
  has_pending_signal_ = false;
  stopping_ = false;

  sigset_t sigmask;
  sigfillset(&sigmask);
  CHECK_EQ(0, pthread_sigmask(SIG_SETMASK, &sigmask, &sigmask));
  int ret = pthread_create(&thread_, nullptr, RunSigintWatchdog, nullptr);
  CHECK_EQ(0, pthread_sigmask(SIG_SETMASK, &sigmask, nullptr));
  if (ret != 0) {
    return ret;
  }
  has_running_thread_ = true;

  RegisterSignalHandler(SIGINT, HandleSignal);
#else
  if (watchdog_disabled_) {
    watchdog_disabled_ = false;
  } else {
    SetConsoleCtrlHandler(WinCtrlCHandlerRoutine, TRUE);
  }
#endif

  return 0;
}


bool SigintWatchdogHelper::Stop() {
  bool had_pending_signal;
  Mutex::ScopedLock lock(mutex_);

  {
    Mutex::ScopedLock list_lock(list_mutex_);

    had_pending_signal = has_pending_signal_;

    if (--start_stop_count_ > 0) {
      has_pending_signal_ = false;
      return had_pending_signal;
    }

#ifdef __POSIX__
    // Set stopping now because it's only protected by list_mutex_.
    stopping_ = true;
#endif

    watchdogs_.clear();
  }

#ifdef __POSIX__
  if (!has_running_thread_) {
    has_pending_signal_ = false;
    return had_pending_signal;
  }

  // Wake up the helper thread.
  uv_sem_post(&sem_);

  // Wait for the helper thread to finish.
  CHECK_EQ(0, pthread_join(thread_, nullptr));
  has_running_thread_ = false;

  RegisterSignalHandler(SIGINT, SignalExit, true);
#else
  watchdog_disabled_ = true;
#endif

  had_pending_signal = has_pending_signal_;
  has_pending_signal_ = false;

  return had_pending_signal;
}


bool SigintWatchdogHelper::HasPendingSignal() {
  Mutex::ScopedLock lock(list_mutex_);

  return has_pending_signal_;
}


void SigintWatchdogHelper::Register(SigintWatchdog* wd) {
  Mutex::ScopedLock lock(list_mutex_);

  watchdogs_.push_back(wd);
}


void SigintWatchdogHelper::Unregister(SigintWatchdog* wd) {
  Mutex::ScopedLock lock(list_mutex_);

  auto it = std::find(watchdogs_.begin(), watchdogs_.end(), wd);

  CHECK_NE(it, watchdogs_.end());
  watchdogs_.erase(it);
}


SigintWatchdogHelper::SigintWatchdogHelper()
    : start_stop_count_(0),
      has_pending_signal_(false) {
#ifdef __POSIX__
  has_running_thread_ = false;
  stopping_ = false;
  CHECK_EQ(0, uv_sem_init(&sem_, 0));
#else
  watchdog_disabled_ = false;
#endif
}


SigintWatchdogHelper::~SigintWatchdogHelper() {
  start_stop_count_ = 0;
  Stop();

#ifdef __POSIX__
  CHECK_EQ(has_running_thread_, false);
  uv_sem_destroy(&sem_);
#endif
}

SigintWatchdogHelper SigintWatchdogHelper::instance;

}  // namespace node
