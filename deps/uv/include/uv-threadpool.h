/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * This file is private to libuv. It provides common functionality to both
 * Windows and Unix backends.
 */

#ifndef UV_THREADPOOL_H_
#define UV_THREADPOOL_H_

/* Forward declarations. */
typedef struct uv__executor_s uv__executor_t;
typedef struct uv__manager_s uv__manager_t;
typedef struct uv__hangman_s uv__hangman_t;
typedef struct uv__worker_s uv__worker_t;
typedef struct uv__executor_channel_s uv__executor_channel_t;

struct uv__work;

/**
 * Executor consists of a manager and a worker.
 * The manager is a watchdog, using an Async and a Timer to see if the worker times out.
 * A worker making forward progress will regularly signal the manager that things are OK.
 *
 * The manager has control over the uv__executor_t struct in which it is defined.
 */
typedef struct uv__executor_s {
	int id;
	struct uv__manager manager; // Static
	struct uv__worker *worker; // May change.
} uv__executor_t;

/**
 * Initialize this executor.
 * Spawns two threads: manager and worker.
 * Returns non-zero on failure.
 */
int uv__executor_init (uv__executor_t *e);

/**
 * Clean up the manager and worker threads.
 * Returns non-zero on failure.
 */
int uv__executor_join (uv__executor_t *e);

/**
 * Create equipment for a new worker, updating fields in the manager.
 * Returns non-zero on failure.
 */
int uv__executor_new_worker (uv__executor_t *e);

/**
 * A manager uses a uv_loop_t to monitor for async and timer events.
 * Async events are triggered by the Worker sending updates -- task begun and task completed.
 * On a "task begun" event, we start a timer.
 * If the timer expires, we go through the failure protocol.
 */
typedef struct uv__manager_s {
	uv_thread_t tid;
	uv_loop_t loop;
	uv_async_t async;
	uv_timer_t timer;

	uv__executor_channel_t *channel; // Executor sets this when it creates a new worker.
	uv__work *last_observed_work; // Do not dereference this!

	// When executor is closing, it sends to the manager, which should then wait on the sem until it knows it's safe to close the async handle. Note the sem is needed because we could set closing in executor, then worker send's, and then manager's async triggers.
	bool closing; // Set during cleanup, before an async_send.
	uv_sem_t final_send_sent;
} uv__manager_t;

void uv__manager_init (uv__manager_t *mgr);

/* Returns non-zero on failure. */
int launch_manager (uv__manager_t *manager, uv__executor_channel_t *channel);

/* Entry point for a manager. */
void manager (void *arg);

/* Entry point for the async and timer events being monitored by a manager. */
void uv__manager_async (uv_async_t *handle);
void uv__manager_timer (uv_timer_t *handle);

/**
 * A worker is managed by a manager, with whom it communicates through a channel.
 * A healthy worker works on tasks from the libuv work queue, send'ing its manager when it begins and completes a task.
 * The manager is in charge of allocating (by allocator) and freeing (by hangman) any new workers.
 */
typedef struct uv__worker_s {
	uv_thread_t tid; // Needed for cleanup by hangman.
	uv__executor_channel_t *channel;
} uv__worker_t;

/* Returns non-zero on failure. */
int launch_worker (uv__worker_t *worker, uv__executor_channel_t *channel);

/* Entry point for a worker. */
void worker (void *w);

/**
 * Communication channel between a manager and its worker.
 * The manager is responsible for allocating and free'ing a channel.
 */
typedef struct uv__executor_channel_s {
	uv_mutex_t mutex; // Acquire to touch any fields.
	uv_async_t *async; // Worker async_send's parent on task begun and task completed. If timed_out, Worker should never send on this again.

	struct uv__work *curr_work; // NULL means no work.
	                            // Need this, not just an ID, so the Manager can access the uv_timeout_cb from the wrapping uv_req_t.
	bool timed_out; // No backsies, this worker is doomed.
} uv__executor_channel_t;

/* Create, destroy a uv__executor_channel_t. */
uv__executor_channel_t * uv__executor_channel_create (uv_async_t *async);
void uv__executor_channel_destroy (uv__executor_channel_t *channel);

/**
 * A hangman is a helper created by a manager to clean up a timed-out worker.
 * It detaches itself and uv_thread_join's its victim.
 * On completion, it calls its uv_killed_cb so that whoever queued the work knows it's really dead.
 *
 * Hangman free's the victim, the victim's channel, and its own memory.
 */
typedef struct uv__hangman_s {
	uv__worker_t self;
	uv__worker_t *victim;
	void *data;
	uv_killed_cb killed_cb;
} uv__hangman_t;

void launch_hangman (uv__worker_t *victim, void *data, uv_killed_cb cb);

/* Entry point for a hangman. */
void hangman (void *arg);

struct uv__work {
  void (*work)(struct uv__work *w); // Set to NULL when work() returns.
  void (*done)(struct uv__work *w, int status);
  struct uv_loop_s* loop;
  void* wq[2]; // QUEUE_EMPTY(&wq) when work() is active.
};

#endif /* UV_THREADPOOL_H_ */
