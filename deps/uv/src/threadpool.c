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

#include "uv-common.h"

#if !defined(_WIN32)
# include "unix/internal.h"
#endif

#include <stdlib.h>

#define MAX_THREADPOOL_SIZE 128

#if 0
#define uv_log(...)
#else
void uv_log (const char *format, ... ){
    static FILE *log_fp = NULL;
		static uv_mutex_t mutex;
    char buffer[512] = {0,};
    va_list args;

    if (log_fp == NULL){
        // init once
        log_fp = fopen("/tmp/uv.log","w");
        if (!log_fp) abort();

				int rc = uv_mutex_init(&mutex);
				if (rc) abort();
    }
    
		uv_mutex_lock(&mutex);

    // va
    va_start (args, format);
    vsnprintf (buffer, 512, format, args);

    // print
    fprintf(log_fp, "[0x%x] %s", uv_thread_self(), buffer);
    fflush(log_fp);

    va_end (args);

		uv_mutex_unlock(&mutex);
}
#endif

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int idle_executors;
static unsigned int nexecutors;
static uv__executor* executors;
static uv__executor default_executors[4];
static QUEUE exit_message;
static QUEUE wq; /* New work is queued to wq and popped by workers. */
static volatile int initialized;

static void uv__cancelled(struct uv__work* w) {
  abort();
}

static void post(QUEUE* q) {
  uv_mutex_lock(&mutex);
  QUEUE_INSERT_TAIL(&wq, q);
  if (idle_executors > 0)
    uv_cond_signal(&cond);
  uv_mutex_unlock(&mutex);
}


#ifndef _WIN32
UV_DESTRUCTOR(static void cleanup(void)) {
  unsigned int i;

  if (initialized == 0)
    return;

  post(&exit_message);

  for (i = 0; i < nexecutors; i++)
    if (uv__executor_join(executors + i))
      abort();

  if (executors != default_executors)
    uv__free(executors);

  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  executors = NULL;
  nexecutors = 0;
  initialized = 0;
}
#endif


static void init_executors(void) {
  unsigned int i;
  const char* val;

  nexecutors = ARRAY_SIZE(default_executors);
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    nexecutors = atoi(val);
  if (nexecutors == 0)
    nexecutors = 1;
  if (nexecutors > MAX_THREADPOOL_SIZE)
    nexecutors = MAX_THREADPOOL_SIZE;

  executors = default_executors;
  if (nexecutors > ARRAY_SIZE(default_executors)) {
    executors = uv__malloc(nexecutors * sizeof(executors[0]));
    if (executors == NULL) {
      nexecutors = ARRAY_SIZE(default_executors);
      executors = default_executors;
    }
  }

  if (uv_cond_init(&cond))
    abort();

  if (uv_mutex_init(&mutex))
    abort();

  QUEUE_INIT(&wq);

  for (i = 0; i < nexecutors; i++) {
		uv_log("init_executors: Initializing executor %i: %p\n", i, executors + i);
    if (uv__executor_init(executors + i))
      abort();
	}

  initialized = 1;
}


#ifndef _WIN32
static void reset_once(void) {
  uv_once_t child_once = UV_ONCE_INIT;
  memcpy(&once, &child_once, sizeof(child_once));
}
#endif


static void init_once(void) {
#ifndef _WIN32
  /* Re-initialize the threadpool after fork.
   * Note that this discards the global mutex and condition as well
   * as the work queue.
   */
  if (pthread_atfork(NULL, NULL, &reset_once))
    abort();
#endif
	uv_log("init_once: initializing executors\n");
  init_executors();
}


void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;
  post(&w->wq);
}


static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !QUEUE_EMPTY(&w->wq) && w->work != NULL;
  if (cancelled)
    QUEUE_REMOVE(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

  if (!cancelled)
    return UV_EBUSY;

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  QUEUE_INSERT_TAIL(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
  uv_mutex_unlock(&loop->wq_mutex);

  return 0;
}

static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !QUEUE_EMPTY(&w->wq) && w->work != NULL;
  if (cancelled)
    QUEUE_REMOVE(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

  if (!cancelled)
    return UV_EBUSY;

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  QUEUE_INSERT_TAIL(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
  uv_mutex_unlock(&loop->wq_mutex);

  return 0;
}

static int uv__work_cancel_active(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
	return 0;
}

void uv__work_done(uv_async_t* handle) {
  struct uv__work* w;
  uv_loop_t* loop;
  QUEUE* q;
  QUEUE wq;
  int err;

  loop = container_of(handle, uv_loop_t, wq_async);
  uv_mutex_lock(&loop->wq_mutex);
  QUEUE_MOVE(&loop->wq, &wq);
  uv_mutex_unlock(&loop->wq_mutex);

  while (!QUEUE_EMPTY(&wq)) {
    q = QUEUE_HEAD(&wq);
    QUEUE_REMOVE(q);

    w = container_of(q, struct uv__work, wq);
    err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    w->done(w, err);
  }
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  req->work_cb(req);
}


static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop, req);

  if (req->after_work_cb == NULL)
    return;

  req->after_work_cb(req, err);
}


int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
  uv__work_submit(loop, &req->work_req, uv__queue_work, uv__queue_done);
  return 0;
}

int uv_cancel(uv_req_t* req) {
  struct uv__work* wreq;
  uv_loop_t* loop;

  switch (req->type) {
  case UV_FS:
    loop =  ((uv_fs_t*) req)->loop;
    wreq = &((uv_fs_t*) req)->work_req;
    break;
  case UV_GETADDRINFO:
    loop =  ((uv_getaddrinfo_t*) req)->loop;
    wreq = &((uv_getaddrinfo_t*) req)->work_req;
    break;
  case UV_GETNAMEINFO:
    loop = ((uv_getnameinfo_t*) req)->loop;
    wreq = &((uv_getnameinfo_t*) req)->work_req;
    break;
  case UV_WORK:
    loop =  ((uv_work_t*) req)->loop;
    wreq = &((uv_work_t*) req)->work_req;
    break;
  default:
    return UV_EINVAL;
  }

  return uv__work_cancel(loop, req, wreq);
}

/***************
 * uv__executor_t
 ***************/

int uv__executor_init (uv__executor_t *e) {
	uv_log("uv__executor_init: Entry\n");

	if (e == NULL) abort();

	// Create a manager.
	uv_log("uv__executor_init: Creating manager\n");
	int rc = uv__manager_init(&e->manager);
	if (rc) abort();
	uv_log("uv__executor_init: Created manager %x\n", e->manager.tid);
	// Create a worker.
	uv_log("uv__executor_init: Creating a new worker\n");
	rc = uv__executor_new_worker(e);
	uv_log("uv__executor_init: Creating worker %x\n", e->worker->tid);
	if (rc) abort();
}

int uv__executor_join (uv__executor_t *e) {
	// TODO The last of my worries.
	uv_log("uv__executor_join: Entry\n");
	return -1;
}

int uv__executor_new_worker (uv__executor_t *e) {
	if (e == NULL) abort();

	uv_log("uv__executor_new_worker: Entry\n");

	// Allocate worker and channel.
	uv__worker_t *worker = (uv__worker_t *) uv__malloc(sizeof(*worker));
	if (worker == NULL) abort();

	uv__executor_channel_t *channel = uv__executor_channel_create(&e->mgr.async);
  if (channel == NULL) abort();

	uv_log("uv__executor_new_worker: worker %p channel %p\n", worker, channel);

	// Update manager. Safe because:
  //    1. worker has not yet started (so manager is blocked in uv_run)
  // or 
  //    2. manager is calling us from uv__manager_timer and has already saved the mutex and started the hangman.
	e->manager.channel = channel;
	e->manager.last_observed_work = NULL;

	int rc = launch_worker(worker, channel);
	return rc;
}

/***************
 * uv__manager_t
 ***************/

int uv__manager_init (uv__manager_t *mgr) {
	if (mgr_ == NULL) abort();
	if (channel == NULL) abort();

	int rc;

	mgr->tid = -1;

	rc = uv_loop_init(&mgr->loop);
	if (rc) abort();

	rc = uv_async_init(&mgr->loop, &mgr->async, uv__manager_async);
	if (rc) abort();

	rc = uv_timer_init(&mgr->loop, &mgr->async, uv__manager_timer);
	if (rc) abort();

	mgr->closing = false;
	rc = uv_sem_init(&mgr->final_send_sent, 0);
	if (rc) abort();

	mgr->channel = NULL;
	mgr->last_observed_work = NULL;
}

int launch_manager (uv__manager_t *mgr) {
	if (mgr_ == NULL) abort();

	int rc = uv_thread_create(&mgr->tid, manager, mgr_);
	uv_log("launch_manager: launched %x (rc %i)\n", mgr->tid, rc);
	return rc;
}

void manager (void *arg) {
	uv__manager_t *self = (uv__worker_t *) m;
	assert(self != NULL);

	int rc = uv_run(&self->loop, UV_RUN_DEFAULT);
	if (rc) abort();
	uv_log("manager: uv_run returned, guess we're done\n");

	uv_log("manager: cleaning up my worker\n");
  uv__executor_t *executor = container_of(self, uv__executor_t, manager);
	launch_hangman(executor->worker, NULL, NULL);

	uv_log("manager: closing my loop\n");
	rc = uv_loop_close(&self->loop);
	if (rc) abort();

	uv_log("manager: Farewell\n");
	return;
}

void uv__manager_async (uv_async_t *handle) {
	if (handle == NULL) abort();
  uv__manager_t *self = container_of(handle, uv__manager_t, async);

	uv_log("uv__manager_async: got async\n");

	bool valid_wakeup = false;
	int rc;

	/* async comes from three places:
   *  1. Worker, work done
   *  2. Worker, new work
   *  3. Executor, closing */

	uv_mutex_lock(&self->channel->mutex);
		// Did the worker wake us up?

		if (channel->curr_work == NULL) {
			// 1. Work done.
			valid_wakeup = true;
			uv_log("uv__manager_async: worker finished work, stopping timer\n");
			rc = uv_timer_stop(&self->timer);
			if (rc) abort();
		}
		else {
			if (channel->curr_work != self->last_observed_work) {
				// 2. New work
				valid_wakeup == true;
				uv_log("uv__manager_async: worker found new work. It is working on %p, I last saw %p\n", channel->curr_work, self->last_observed_work);

				// Reset the timer.
				uv_log("uv__manager_async: Starting a timer\n");
				rc = uv_timer_stop(&self->timer);
				if (rc) abort();

				// TODO Choose timeout somehow, e.g. from input or from env var.
				uint64_t timeout_ms = 1*1000; /* 1 second */
				rc = uv_timer_start(&self->timer, uv__manager_timer, timeout_ms, 0);
				if (rc) abort();
			}
			else {
				uv_log("uv__manager_async: worker is still working on the same work, it must not have woken me\n");
			}
		}
		self->last_observed_work = channel->curr_work; // Update observation.
	uv_mutex_unlock(&self->channel->mutex);

	if (self->closing) {
		// 3. Executor, closing
		valid_wakeup = true;
		uv_log("uv__manager_async: closing, so closing my handles\n");
		uv_sem_wait(&self->final_send_sent);
		uv_close(&self->async, NULL);
		uv_close(&self->timer, NULL);
	}

	if (!valid_wakeup) abort();

	return;
}

void uv__manager_timer (uv_timer_t *handle) {
	if (handle == NULL) abort();
  uv__manager_t *self = container_of(handle, uv__manager_t, timer);
  uv__executor_t *executor = container_of(self, uv__executor_t, manager);
	uv_log("uv__manager_timer: got timer\n");

	int rc;

	// If we abort the Worker, we create a new channel. Remember the old channel's mutex's address.
	uv_mutex_t *channel_mutex = (&mgr->channel->mutex);

	uv_mutex_lock(channel_mutex);
		if (self->last_observed_work == self->channel->curr_work) {
			// Still working on same work as last time, a legitimate timeout.
			uv_log("uv__manager_timer: got timer, worker still working on same work.\n");

			// Inform owner that his task has timed out. See what response he wants.
			uv_log("uv__manager_timer: Calling timed_out_cb to see what to do\n");
			uv_work_t *req = container_of(self->last_observed_work, uv_work_t, work_req);
			void *hangman_data = NULL;
			uint64_t grace_period_ms = 0;
			if (req->timed_out_cb)
				grace_period_ms = req->timed_out_cb(req, &hangman_data);
			if (grace_period_ms) {
				// Reset timer for the grace period.
				uv_log("uv__manager_timer: timed_out cb requested grace period of %llu ms. Resetting timer.\n", grace_period_ms);
				rc = uv_timer_stop(&self->timer);
				if (rc) abort();
				rc = uv_timer_start(&self->timer, uv__manager_timer, grace_period_ms, 0);
				if (rc) abort();
			}
			else {
				// Owner agrees we can time out the request. Hangman.
				uv_log("uv__manager_timer: No grace period requested. Launching hangman.\n");
				mgr->channel->timed_out = true;
				// Hangman gets the old worker, so it can clean up the associated memory allocated in uv__executor_new_worker.
				launch_hangman(executor->worker, hangman_data, req->killed_cb);
		
				// Since we are still holding the channel mutex, we are guaranteed that the worker won't call uv_async_send again. Safe to give a new Worker this async without confusion.
				mgr->last_observed_work = NULL;

				uv_log("uv__manager_timer: Creating new worker\n");
				rc = uv__executor_new_worker(executor);
				uv_log("uv__manager_timer: New worker: %x\n", executor->worker->tid);
				if (rc) abort();
			}
		}
		else {
			// Timer expired but work has changed. NBD.
			uv_log("uv__manager_timer: got timer, but it looks like the worker finished the work already. Doing nothing.\n");
		}
	uv_mutex_unlock(channel_mutex);

	return;
}

/***************
 * uv__worker_t
 ***************/

int launch_worker (uv__worker_t *worker_, uv__executor_channel_t *channel) {
	if (worker_ == NULL) abort();
	if (channel == NULL) abort();

	worker_->channel = channel;
	int rc = uv_thread_create(&worker->tid, worker, worker_);
	uv_log("launch_worker: launched %x (rc %i)\n", worker->tid, rc);
	return rc;
}

/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
void worker (void *arg) {
  struct uv__work* w;
  QUEUE* q;

	uv__worker_t *self = (uv__worker_t *) arg;
	assert(self != NULL);

  for (;;) {
    uv_mutex_lock(&mutex);

    while (QUEUE_EMPTY(&wq)) {
      idle_executors += 1;
      uv_cond_wait(&cond, &mutex);
      idle_executors -= 1;
    }

    q = QUEUE_HEAD(&wq);

    if (q == &exit_message)
      uv_cond_signal(&cond);
    else {
      QUEUE_REMOVE(q);
      QUEUE_INIT(q);  /* Signal uv_cancel() that the work req is
                             executing. */
    }

    uv_mutex_unlock(&mutex);

    if (q == &exit_message)
      break;

		/* We got work! */
    w = QUEUE_DATA(q, struct uv__work, wq);
		uv_log("worker: Got work %p\n", w);

		uv_mutex_lock(&self->channel->mutex);
			// Check if we timed out.
			if (w->timed_out) {
				uv_log("worker: Timed out??\n");
				uv_mutex_unlock(&self->channel->mutex);
				abort(); // TODO Re-queue w and return, but this is really unlikely.	
			}

			// Tell manager we have a new task
			uv_log("worker: Telling manager we have new work\n");
			self->channel->curr_work = w;
			uv_async_send(self->channel->async);
		uv_mutex_unlock(&self->channel->mutex);

		// Do the work
    w->work(w);

		uv_mutex_lock(&self->channel->mutex);
			// Check if we timed out.
			if (self->channel->timed_out) {
				// There's a hangman out for our blood.
				// He will clean up our corpse.
				uv_log("worker: Timed out, returning\n");
				uv_mutex_unlock(&self->channel->mutex);
				return;
			}

			// Tell manager we finished our task.
			self->channel->curr_work = NULL;
			uv_async_send(self->channel->async);
		uv_mutex_unlock(&self->channel->mutex);

		// Prepare for next task.
    uv_mutex_lock(&w->loop->wq_mutex);

    w->work = NULL;  /* Signal uv_cancel() that the work req is done
                        executing. */
    QUEUE_INSERT_TAIL(&w->loop->wq, &w->wq);
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);
  }

	uv_log("worker: Farewell\n");
	return;
}

/***************
 * uv__hangman_t
 ***************/

void launch_hangman (uv__worker_t *victim, void *data, uv_killed_cb cb) {
	uv__hangman_t *hangman_ = uv__malloc(sizeof(*hangman));
	if (hangman == NULL) abort()

	uv_log("launch_hangman: victim %p data %p\n", victim, data);

	hangman_->victim = victim;
	hangman_->data = data;
	hangman_->cb = cb;

	int rc = uv_thread_create(&hangman->self, hangman_, hangman);
	if (rc) abort();
	return;
}

void hangman (void *h) {
	uv_log("hangman: Entry\n");
	uv__hangman_t *hangman_ = (uv__hangman_t *) h;
	if (hangman_ == NULL) abort();

	// Caller doesn't need to join() on us.
	int rc = uv_thread_detach(&hangman_->self);
	if (rc) abort();

	if (hangman_->victim != NULL) {
		// Cancel the victim.
    // It might already have finished its task, seen channel->timed_out, and returned, so ignore the uv_thread_cancel rc.
		uv_log("hangman: Cancel'ing victim %d\n", hangman_->victim->tid);
		uv_thread_cancel(&hangman_->victim->tid);

		// Join the victim.
		uv_log("hangman: Joining victim worker %d\n", hangman_->victim->tid);
		rc = uv_thread_join(&hangman_->victim->tid);
		if (rc) abort();

		// Call the cb if we have one.
		if (hangman_->killed_cb != NULL) {
			uv_log("hangman: Calling killed_cb with %p\n", hangman_->data);
			hangman_->killed_cb(hangman_->data);
		}
		
		// Clean up
		uv_log("hangman: Releasing victim's channel and memory\n");
		uv__executor_channel_destroy(hangman_->victim->channel);
		uv__free(hangman_->victim);
	}

	// Clean up self
	uv__free(hangman_);

	uv_log("hangman: Farewell\n");
	return;
}

/***************
 * uv__executor_channel_t
 ***************/

uv__executor_channel_t * uv__executor_channel_create (uv_async_t *async) {
	if (async == NULL) abort();
	uv__executor_channel_t *ret = uv__malloc(sizeof(*ret));
	if (ret == NULL) abort();

	ret->async = async;
	if (uv_mutex_init(&ret->mutex))
		abort();

	ret->curr_work = NULL;
	ret->timed_out = false;

	return ret;
}

void uv__executor_channel_destroy (uv__executor_channel_t *channel) {
	if (channel == NULL) abort();

	uv_mutex_destroy(&channel->mutex);
	uv__free(channel);
}

