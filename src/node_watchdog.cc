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
#include <stdarg.h>

namespace node {
    static void mark_not_cancelable (void) {
        int old;
        int rc = uv_thread_setcancelstate(UV_CANCEL_DISABLE, &old);
        if (rc) abort();
    }

    static void mark_cancelable (void) {
        int old;
        int rc = uv_thread_setcancelstate(UV_CANCEL_ENABLE, &old);
        if (rc) abort();
    }

    /* Embed a prefix into buf. */
    static char * _mylog_embed_prefix (int verbosity, char *buf, int len) {
        struct timespec now;
        char now_s[64];
        struct tm t;

        assert(buf);

        assert(clock_gettime(CLOCK_REALTIME, &now) == 0);
        localtime_r(&now.tv_sec, &t);

        memset(now_s, 0, sizeof now_s);
        strftime(now_s, sizeof now_s, "%a %b %d %H:%M:%S", &t);
        snprintf(now_s + strlen(now_s), sizeof(now_s) - strlen(now_s), ".%09ld", now.tv_nsec);

        snprintf(buf, len, "%-3i %-32s", verbosity, now_s);
        return buf;
    }

    void node_log (int verbosity, const char *format, ... ){
        int rc;
        static FILE *log_fp = NULL;
        static uv_mutex_t log_mutex;
        char buffer[512] = {0,};
        va_list args;

        if (log_fp == NULL){
            /* Init once */
            log_fp = fopen("/tmp/node_watchdog.log","w");
            if (!log_fp) abort();

            rc = uv_mutex_init(&log_mutex);
            if (rc) abort();
        }

        va_start(args, format);
        _mylog_embed_prefix(verbosity, buffer, sizeof(buffer));
        vsnprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), format, args);
        mark_not_cancelable();
        uv_mutex_lock(&log_mutex);
        fprintf(log_fp, "[%lu] %s", uv_thread_self(), buffer);
        uv_mutex_unlock(&log_mutex);
        mark_cancelable();

        va_end (args);

        fflush(log_fp);
    }


    Watchdog::Watchdog(uint64_t ms, WatchdogFunc aborted_cb, WatchdogFunc timeout_cb, void *data)
    : aborted_cb_(aborted_cb), timeout_cb_(timeout_cb), data_(data), timed_out_(false), aborted_(false) {

  int rc;
  loop_ = new uv_loop_t;
  CHECK(loop_);
  rc = uv_loop_init(loop_); // NB uv_loop_init doesn't initialize a worker pool. It's on the first time you uv_queue_work.
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
	uv_stop(w->loop_); // Signal timer not to go off, but no guarantee.

	w->aborted_ = true;

	// Only call aborted if not timed out.
	if (!w->timed_out_ && w->aborted_cb_ != NULL)
		w->aborted_cb_(w->data_);
}


void Watchdog::Timer(uv_timer_t* timer) {
  Watchdog* w = ContainerOf(&Watchdog::timer_, timer);
  uv_stop(w->loop_); // Signal async not to go off, but no guarantee.

  w->timed_out_ = true;

	// Only call timeout if not aborted.
	if (!w->aborted_ && w->timeout_cb_ != NULL)
		w->timeout_cb_(w->data_);
}

TimeoutWatchdog::TimeoutWatchdog(v8::Isolate* isolate, long timeout_ms)
    : isolate_(isolate), timeout_ms_(timeout_ms), stopping_(false) {

  int rc;

	/* Initialize all the fields. */

  loop_ = new uv_loop_t;
  CHECK(loop_);
  rc = uv_loop_init(loop_); // NB uv_loop_init doesn't initialize a worker pool. It's on the first time you uv_queue_work.
  if (rc != 0) {
    FatalError("node::TimeoutWatchdog::TimeoutWatchdog()",
               "Failed to initialize uv loop.");
  }

  rc = uv_async_init(loop_, &async_, &TimeoutWatchdog::Async);
  CHECK_EQ(0, rc);

  rc = uv_timer_init(loop_, &timer_);
  CHECK_EQ(0, rc);

	rc = uv_mutex_init(&lock_);
	CHECK(rc == 0);

	CHECK(pending_async_ids_.empty());
	stack_num_ = 0;
	time_at_stack_change_ms_ = 0;

	leashed_ = false;
	time_at_leash_ms_ = 0;
	stack_num_at_leash_ = -1;
	stack_num_at_unleash_ = -1;
	threw_while_leashed_ = false;

	timer_start_time_ms_ = 0;
	stack_num_at_timer_start_ = -1;

	stopping_ = false;

	rc = uv_sem_init(&stopped_, 0);
	CHECK(rc == 0);

	/* Lastly, start the inhabiting TW thread. */
  rc = uv_thread_create(&thread_, &TimeoutWatchdog::Run, this);
  CHECK_EQ(0, rc);
}

TimeoutWatchdog::~TimeoutWatchdog() {
	/* Clean up the TW thread. */
  node_log(2, "TimeoutWatchdog::~TimeoutWatchdog: Cleaning up TW thread\n");

	// Signal TW thread that we're done.
	uv_mutex_lock(&lock_);
		node_log(2, "TimeoutWatchdog::~TimeoutWatchdog: Signaling TW thread\n");
		stopping_ = true; 
		uv_async_send(&async_);
	uv_mutex_unlock(&lock_);

	node_log(2, "TimeoutWatchdog::~TimeoutWatchdog: Waiting for TW thread to down the semaphore\n");
	uv_sem_wait(&stopped_);

  node_log(2, "TimeoutWatchdog::~TimeoutWatchdog: Joining thread\n");
  uv_thread_join(&thread_);

  // UV_RUN_DEFAULT so that libuv has a chance to clean up.
  node_log(2, "TimeoutWatchdog::~TimeoutWatchdog: Final uv_run for libuv cleanup\n");
  uv_run(loop_, UV_RUN_DEFAULT);

  int rc = uv_loop_close(loop_);
  CHECK_EQ(0, rc);
  delete loop_;
  loop_ = nullptr;

	uv_mutex_destroy(&lock_);
}

void TimeoutWatchdog::BeforeHook(long async_id) {
	uv_mutex_lock(&lock_);
	CHECK(!stopping_);

	bool was_empty = pending_async_ids_.empty();
	pending_async_ids_.push(async_id);

	/* TODO What to do about TIMERWRAP vs. individual timers? This scheme bills the cumulative time to the framework-level wrapper.
	 *      Probably same issue with an EventEmitter? */
  if (was_empty) {
		stack_num_++;
		time_at_stack_change_ms_ = uv_hrtime() / 1000000;
		node_log(2, "TimeoutWatchdog::BeforeHook: was_empty, now stack_num_ %ld\n", stack_num_);
		uv_async_send(&async_); // Signal wd thread: change in state.
	}

	uv_mutex_unlock(&lock_);

	return;
}

void TimeoutWatchdog::AfterHook(long async_id) {
	uv_mutex_lock(&lock_);

	CHECK(!stopping_);
	CHECK(!leashed_);
	CHECK(!pending_async_ids_.empty());

	/* Pop ID, see if state changed. */
	pending_async_ids_.pop();
	bool now_empty = pending_async_ids_.empty();

	// Signal wd thread: change in state.
	if (now_empty) {
		node_log(2, "TimeoutWatchdog::AfterHook: now_empty\n");
		uv_async_send(&async_);
	}

	uv_mutex_unlock(&lock_);

	return;
}

uint64_t TimeoutWatchdog::Leash (void) {
	node_log(2, "TimeoutWatchdog::Leash: entry\n");

	uv_mutex_lock(&lock_);

	CHECK_EQ(leashed_, false);
	leashed_ = true;

	if (pending_async_ids_.empty()) {
		node_log(2, "TimeoutWatchdog::Leash: startup code, not in an async region\n");
		uv_mutex_unlock(&lock_);
		return timeout_ms_;
	}

	time_at_leash_ms_ = uv_hrtime() / 1000000;
	stack_num_at_leash_ = stack_num_;
	node_log(2, "TimeoutWatchdog::Leash: stack_num_at_leash_ %ld\n", stack_num_);
	uv_async_send(&async_); // Signal leash change.

	uv_mutex_unlock(&lock_);

  /* Figure out how long the timer had left. */
	uint64_t since_start_ms = time_at_leash_ms_ - timer_start_time_ms_;
	uint64_t remaining_ms = (timeout_ms_ < since_start_ms) ? 0 : timeout_ms_ - since_start_ms;

	node_log(2, "TimeoutWatchdog::Leash: Leashed with %lu remaining\n", remaining_ms);
	return remaining_ms;
}

void TimeoutWatchdog::Unleash (bool threw) {
	node_log(2, "TimeoutWatchdog::Unleash: entry\n");

	uv_mutex_lock(&lock_);

	CHECK_EQ(leashed_, true);
	leashed_ = false;

	if (pending_async_ids_.empty()) {
		node_log(2, "TimeoutWatchdog::Unleash: startup code, not in an async region\n");
		uv_mutex_unlock(&lock_);
		return;
	}

	threw_while_leashed_ = threw;
	stack_num_at_unleash_ = stack_num_;

	CHECK(stack_num_at_leash_ == stack_num_at_unleash_);

	node_log(2, "TimeoutWatchdog::Unleash: threw_while_leashed %d stack_num_at_leash_ %ld\n", threw, stack_num_);
	uv_async_send(&async_); /* Signal leash change. */

	uv_mutex_unlock(&lock_);

	return;
}

void TimeoutWatchdog::Run(void* arg) {
  TimeoutWatchdog* wd = static_cast<TimeoutWatchdog*>(arg);

	node_log(2, "TimeoutWatchdog::Run: Calling uv_run\n");
  uv_run(wd->loop_, UV_RUN_DEFAULT); // Run until we call uv_stop due to signal from TimeoutWatchdog::~TimeoutWatchdog.
	node_log(2, "TimeoutWatchdog::Run: returning\n");
}

/**
 * At some point, someone uv_async_send'd.
 *  BeforeHook, AfterHook, Leash, Unleash, wrapping up, or any sequence.
 * Since uv_async_send can be coalesced, we can't assume anything about the state now.
 * Must reason from our state variables.
 */
void TimeoutWatchdog::Async(uv_async_t* async) {
  TimeoutWatchdog *w = ContainerOf(&TimeoutWatchdog::async_, async);

  node_log(2, "TimeoutWatchdog::Async: entry\n");
	uv_mutex_lock(&w->lock_);

	bool any_async_ids = !w->pending_async_ids_.empty();
	int rc;
	uint64_t since_ms;
	uint64_t remaining_ms;

	// 1. If we're stopping, bail.
	if (w->stopping_) {
		node_log(2, "TimeoutWatchdog::Async: stopping_, cleaning up and calling uv_stop\n");

		rc = uv_timer_stop(&w->timer_);
		CHECK(rc == 0);
        uv_close(reinterpret_cast<uv_handle_t*>(&w->async_), nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&w->timer_), nullptr);

		uv_stop(w->loop_);

		uv_sem_post(&w->stopped_);
		goto UNLOCK_AND_RETURN;
	}

	// 2. If we're leashed, clear the current timer. Unleash is checked below.
	if (w->leashed_) {
		node_log(2, "TimeoutWatchdog::Async: leashed, clearing the timer\n");
		rc = uv_timer_stop(&w->timer_);
		CHECK(rc == 0);
		goto UNLOCK_AND_RETURN;
	}

	// 3. If there's no stack, nothing to do.
	if (!any_async_ids) {
		node_log(2, "TimeoutWatchdog::Async: no stack\n");
		rc = uv_timer_stop(&w->timer_);
		CHECK(rc == 0);
		goto UNLOCK_AND_RETURN;
	}

	// We are neither stopping nor leashed.
	// Since we are here, there must have been at least one stack change or at least one Unleash.
	// It may be that there was a stack change and we should start a timer.
	// It may be that a Leash-Unleash wiped our timer and we should pick up where we left off.
	// We can't just restart a timer from timeout_ms_ because of something like this:
	//   `while(1) { fs.readFileSync('/tmp/x'); }`.
	// We have to account for the time used up in the Leashed region.

	// 4. If the stack has changed since we last started a timer, our current timer is invalid and we should start anew.
	if (w->stack_num_ != w->stack_num_at_timer_start_) {
		node_log(2, "TimeoutWatchdog::Async: stack has changed, starting a timer\n");
		w->_StartTimer(w->timeout_ms_);
		goto UNLOCK_AND_RETURN;
	}

	// 5. It is the same stack for which we last started a timer. That means there was an Unleash.
	CHECK(w->stack_num_ == w->stack_num_at_unleash_);
	if (w->threw_while_leashed_) {
		node_log(2, "TimeoutWatchdog::Async: There was an Unleash, but we threw while leashed. Starting a fresh timer\n");
		w->_StartTimer(w->timeout_ms_);
		goto UNLOCK_AND_RETURN;
	}
	else {
		node_log(2, "TimeoutWatchdog::Async: There was an Unleash, but it did not throw. Resuming a timer\n");
		since_ms = (uv_hrtime() / 1000000) - w->timer_start_time_ms_;
		if (w->timeout_ms_ < since_ms) {
			node_log(2, "TimeoutWatchdog::Async: We should timeout now!\n");
			// TIMEOUT
			w->isolate()->Timeout(); // TODO Is it safe to call Timeout if the previous Timeout interrupt has been handled but the Timeout it threw hasn't cleared yet? Need to modify V8-land? Will this ever happen with a "reasonable" timeout_ms_?
			remaining_ms = w->timeout_ms_; // Guard timeout handler.
		}
		else {
			remaining_ms = w->timeout_ms_ - since_ms;
			node_log(2, "TimeoutWatchdog::Async: We still have %llu ms\n", remaining_ms);
		}
		w->_StartTimer(remaining_ms);
		goto UNLOCK_AND_RETURN;
	}

	node_log(2, "TimeoutWatchdog::Async: ???\n");
	CHECK(0 == 1); // NOT_REACHED

UNLOCK_AND_RETURN:
	uv_mutex_unlock(&w->lock_);
	return;
}

/* Caller should hold lock_. */
void TimeoutWatchdog::_StartTimer(uint64_t expiry_ms) {
	uint64_t now_ms = uv_hrtime() / 1000000;
	node_log(2, "TimeoutWatchdog::_StartTimer: entry at %lu, expiry_ms %lu\n", now_ms, expiry_ms);

	CHECK(!pending_async_ids_.empty());
	CHECK(stack_num_ != -1);

	uv_timer_stop(&timer_); // Make sure it's stopped. Now sure what happens if I start a started timer, but this is cheap since we only have one timer in our uv timer heap.
	uint64_t repeat = 0; // Don't repeat; libuv's timers go off at timeout_ms_ intervals accounting for time shaving. We restart with full timeout ourselves.
	int rc = uv_timer_start(&timer_, &TimeoutWatchdog::Timer, expiry_ms, repeat);
	CHECK_EQ(0, rc);

	if (expiry_ms < timeout_ms_) {
		// We are resuing timing an existing stack in response to Leash/Unleash.
		CHECK(stack_num_at_timer_start_ == stack_num_);
	}
	else {
		timer_start_time_ms_ = now_ms;
		stack_num_at_timer_start_ = stack_num_;
	}

	node_log(2, "TimeoutWatchdog::_StartTimer: timer_start_time_ms_ %llu stack_num_at_timer_start_ %ld\n", now_ms, stack_num_);
	return;
}

void TimeoutWatchdog::Timer(uv_timer_t* timer) {
  TimeoutWatchdog* w = ContainerOf(&TimeoutWatchdog::timer_, timer);

	uint64_t now_ms = uv_hrtime() / 1000000; /* 10e9 / 10e6 = 10e3 */
	bool should_throw = false;

  node_log(2, "TimeoutWatchdog::Timer: entry at %llu\n", now_ms);
	uv_mutex_lock(&w->lock_);

	/* Reasons not to throw a timeout. */

	// 1. We should stop.
	if (w->stopping_) {
		node_log(2, "TimeoutWatchdog::Timer: stopping_, returning early\n");
		goto UNLOCK_AND_RETURN;
	}

	// 2. The loop has moved on, and Async hasn't had a chance to go yet. */
	if (w->stack_num_at_timer_start_ != w->stack_num_) {
		node_log(2, "TimeoutWatchdog::Timer: timer is for an old async id, returning early\n");
		goto UNLOCK_AND_RETURN;
	}

	// 3. We never throw while leashed.
	if (w->leashed_) {
		node_log(2, "TimeoutWatchdog::Timer: leashed_, returning early\n");
		goto UNLOCK_AND_RETURN;
	}

	// 4. We might want to reset the timer but not throw.
	//    If the current timer predates a Leash that threw in the same stack,
	//    then we should reset the timer but not throw.
	should_throw = true;
	if (w->stack_num_at_unleash_ == w->stack_num_ &&
		  w->timer_start_time_ms_ < w->time_at_leash_ms_ &&
			w->threw_while_leashed_) {
		node_log(2, "TimeoutWatchdog::Timer:: Timer predates most recent leash and that leash threw. Restarting the timer.\n");
		should_throw = false;
	}

	if (should_throw) {
		/* No more reasons, so we'll throw. */
		node_log(2, "TimeoutWatchdog::Timer: Throwing a timeout\n");
		w->isolate()->Timeout(); // TODO Is it safe to call Timeout if the previous Timeout interrupt has been handled but the Timeout it threw hasn't cleared yet? Need to modify V8-land? Will this ever happen with a "reasonable" timeout_ms_?
	}

	node_log(2, "TimeoutWatchdog::Timer: Resetting the timer in case of a bad exception handler.\n");
	w->_StartTimer(w->timeout_ms_);

UNLOCK_AND_RETURN:
  node_log(2, "TimeoutWatchdog::Timer: return\n");
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
