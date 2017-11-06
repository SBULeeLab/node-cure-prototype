/**
 * Structs and APIs for a worker pool that can time out
 * long-running tasks.
 */

#ifndef UV_THREADPOOL_PRIV_H_
#define UV_THREADPOOL_PRIV_H_

#include "uv-threadpool.h"

/* Forward declarations. */
typedef struct uv__executor_s uv__executor_t;
typedef struct uv__manager_s uv__manager_t;
typedef struct uv__hangman_s uv__hangman_t;
typedef struct uv__worker_s uv__worker_t;
typedef struct uv__executor_channel_s uv__executor_channel_t;

/* Declare this first so an executor knows its size. */

/**
 * A manager uses a uv_loop_t to monitor for async and timer events.
 * Async events are triggered by the Worker sending updates -- task begun and task completed.
 * On a "task begun" event, we start a timer.
 * If the timer expires, we go through the failure protocol.
 */
struct uv__manager_s {
	uv_thread_t tid;
	uv_loop_t loop;
	uv_async_t async;
	uv_timer_t timer;

	uv__executor_channel_t *channel; /* Executor sets this when it creates a new worker. */
	struct uv__work *last_observed_work; /* Do not dereference this! */

	/* When executor is closing, it sends to the manager, which should then wait on the sem until it knows it's safe to close the async handle. Note the sem is needed because we could set closing in executor, then worker send's, and then manager's async triggers. */
	int closing; /* Set during cleanup, before an async_send. */
	uv_sem_t final_send_sent;
};

/* Returns non-zero on failure. */
int launch_manager (uv__manager_t *manager);

/* Entry point for a manager. */
void manager (void *arg);

/* Entry point for the async and timer events being monitored by a manager. */
void uv__manager_async (uv_async_t *handle);
void uv__manager_timer (uv_timer_t *handle);


/**
 * Executor consists of a manager and a worker.
 * The manager is a watchdog, using an Async and a Timer to see if the worker times out.
 * A worker making forward progress will regularly signal the manager that things are OK.
 *
 * The manager has control over the uv__executor_t struct in which it is defined.
 */
struct uv__executor_s {
	int id;
	uv__manager_t manager; /* Static */
	uv__worker_t *worker; /* May change. */
};

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
 * A worker is managed by a manager, with whom it communicates through a channel.
 * A healthy worker works on tasks from the libuv work queue, send'ing its manager when it begins and completes a task.
 * The manager is in charge of allocating (by allocator) and freeing (by hangman) any new workers.
 */
struct uv__worker_s {
	uv_thread_t tid; /* Needed for cleanup by hangman. */
	uv__executor_channel_t *channel;
};

/* Returns non-zero on failure. */
int launch_worker (uv__worker_t *worker, uv__executor_channel_t *channel);

/* Entry point for a worker. */
void worker (void *w);

/**
 * Communication channel between a manager and its worker.
 * The manager is responsible for allocating and free'ing a channel.
 */
struct uv__executor_channel_s {
	uv_mutex_t mutex; /* Acquire to touch any fields. */
	uv_async_t *async; /* Worker async_send's parent on task begun and task completed. If timed_out, Worker should never send on this again. */

	struct uv__work *curr_work; /* NULL means no work. */
	                            /* Need this, not just an ID, so the Manager can access the uv_timeout_cb from the wrapping uv_req_t. */
	int timed_out; /* No backsies, this worker is doomed. */
};

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
struct uv__hangman_s {
	uv_thread_t tid;
	uv__worker_t *victim;
	void (*killed_cb)(struct uv__work *w);
};

void launch_hangman (uv__worker_t *victim, void (*killed_cb)(struct uv__work *w));

/* Entry point for a hangman. */
void hangman (void *arg);

#endif /* UV_THREADPOOL_PRIV_H_ */
