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

#include "threadpool-priv.h"

#if 0
#define uv_log(...)
#else

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

void uv_log (int verbosity, const char *format, ... ){
	int rc;
	static FILE *log_fp = NULL;
	static uv_mutex_t log_mutex;
	char buffer[512] = {0,};
	va_list args;

	if (log_fp == NULL){
		/* Init once */
		log_fp = fopen("/tmp/uv.log","w");
		if (!log_fp) abort();

		rc = uv_mutex_init(&log_mutex);
		if (rc) abort();
	}

	uv_mutex_lock(&log_mutex);

	/* va */
	va_start(args, format);
	_mylog_embed_prefix(verbosity, buffer, sizeof(buffer));
	vsnprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), format, args);

	/* print */
	fprintf(log_fp, "[%lu] %s", uv_thread_self(), buffer);
	fflush(log_fp);

	va_end (args);

	uv_mutex_unlock(&log_mutex);
}
#endif

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int idle_executors;
static unsigned int n_executors;
static uv__executor_t* executors;
static uv__executor_t default_executors[4];
static QUEUE exit_message;
static QUEUE wq; /* New work is queued to wq and popped by workers. */
static volatile int initialized;

static int uv__manager_init (uv__manager_t *mgr);

static void uv__cancelled(struct uv__work* w) {
  abort();
}

static void queue_completed_work (struct uv__work *w) {
	if (w == NULL) abort();

	uv_mutex_lock(&w->loop->wq_mutex);

	w->work = NULL;  /* Signal uv_cancel() that the work req is done
											executing. */
	QUEUE_INSERT_TAIL(&w->loop->wq, &w->wq);
	uv_async_send(&w->loop->wq_async);
	uv_mutex_unlock(&w->loop->wq_mutex);
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

  for (i = 0; i < n_executors; i++)
    if (uv__executor_join(executors + i))
      abort();

  if (executors != default_executors)
    uv__free(executors);

  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  executors = NULL;
  n_executors = 0;
  initialized = 0;
}
#endif


static void init_executors(void) {
  unsigned int i;
  const char* val;

  n_executors = ARRAY_SIZE(default_executors);
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    n_executors = atoi(val);
  if (n_executors == 0)
    n_executors = 1;
  if (n_executors > MAX_THREADPOOL_SIZE)
    n_executors = MAX_THREADPOOL_SIZE;

	uv_log(1, "init_executors: %d executors\n", n_executors);
  executors = default_executors;
  if (n_executors > ARRAY_SIZE(default_executors)) {
    executors = uv__malloc(n_executors * sizeof(executors[0]));
    if (executors == NULL) {
      n_executors = ARRAY_SIZE(default_executors);
      executors = default_executors;
    }
  }

  if (uv_cond_init(&cond))
    abort();

  if (uv_mutex_init(&mutex))
    abort();

  QUEUE_INIT(&wq);

  for (i = 0; i < n_executors; i++) {
		uv_log(1, "init_executors: Initializing executor %i: %p\n", i, executors + i);
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
	uv_log(1, "init_once: initializing executors\n");
  init_executors();
}

/* Internal entrance into the threadpool.
 * Called from uv_queue_work with a uv_req_t, called from elsewhere (e.g. fs.c) and bypassing the uv_queue_work API. */
void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     void (*work)(struct uv__work* w),
                     uint64_t (*timed_out)(struct uv__work *w, void **killed_dat), /* See uv_timed_out_cb. */
                     void (*done)(struct uv__work* w, int status),
                     void (*killed)(void *dat)) { /* See uv_killed_cb. */
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
	w->timed_out = timed_out;
  w->done = done;
	w->killed = killed;

	w->state_queued = 0;
	w->state_assigned = 0;
	w->state_timed_out = 0;
	w->state_done = 0;
	w->state_canceled = 0;

	w->state_queued = 1;

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

  if (!cancelled) {
    return UV_EBUSY;
	}

  w->state_canceled = 1;

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  QUEUE_INSERT_TAIL(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
  uv_mutex_unlock(&loop->wq_mutex);

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

    /* Choose an appropriate err code. */
		if (w->work == uv__cancelled)
			err = UV_ECANCELED;
		else if (w->state_timed_out)
			err = UV_ETIMEDOUT;
		else
			err = 0;

    w->done(w, err);
  }
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

	if (!w->state_queued) abort();
	w->state_assigned = 1;

  req->work_cb(req);
}

static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

	if (!w->state_assigned) abort();
	w->state_done = 1;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop, req);

  if (req->after_work_cb == NULL)
    return;

  req->after_work_cb(req, err);
}

static uint64_t uv__queue_timed_out(struct uv__work* w, void **dat) {
	uint64_t ret;
  uv_work_t* req = container_of(w, uv_work_t, work_req);

	if (!w->state_assigned) abort();
	if (w->state_done) abort();

  ret = req->timed_out_cb(req, dat); /* NOT INVOKED FROM MAIN THREAD. */
	if (ret == 0)
		w->state_timed_out = 1;
	return ret;
}

/* External entrance into threadpool.
 * We run req with the associated callbacks, using intermediate uv__queue_X callbacks to call them appropriately. */
int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
									uv_timed_out_cb timed_out_cb,
                  uv_after_work_cb after_work_cb,
									uv_killed_cb killed_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
	req->timed_out_cb = timed_out_cb;
	req->killed_cb = killed_cb;
	/* We set the intermediate killed_cb to the input value because we call it after calling uv_after_work_cb.
	 * The req may have been de-allocated by then. */
  uv__work_submit(loop, &req->work_req, uv__queue_work, uv__queue_timed_out, uv__queue_done, killed_cb);
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
	int rc;
	uv_log(1, "uv__executor_init: Entry\n");

	if (e == NULL) abort();

	memset(e, 0, sizeof(*e));

	/* Create a manager. */
	uv_log(1, "uv__executor_init: Creating manager\n");
	rc = launch_manager(&e->manager);
	if (rc) abort();
	uv_log(1, "uv__executor_init: Created manager %lu\n", e->manager.tid);

	/* Create a worker. */
	uv_log(1, "uv__executor_init: Creating a new worker\n");
	rc = uv__executor_new_worker(e);
	uv_log(1, "uv__executor_init: Created worker %lu\n", e->worker->tid);
	if (rc) abort();
	return 0;
}

int uv__executor_join (uv__executor_t *e) {
  uv_log(1, "uv__executor_join: Entry\n");

  /* Tell manager we're closing. */
  uv_log(1, "uv__executor_join: Signaling manager %lu\n", e->manager.tid);
  e->manager.closing = 1;
  uv_sem_post(&e->manager.final_send_sent);
  uv_async_send(&e->manager.async);

  /* Wait for manager to return. */
  uv_log(1, "uv__executor_join: Join'ing manager %lu\n", e->manager.tid);
  uv_thread_join(&e->manager.tid);

  /* The hangman for the last worker has begun, but may not have completed.
   * If we're not shutting down, that's OK.
   * If we're shutting down, it doesn't matter anyway. */

	return 0;
}

int uv__executor_new_worker (uv__executor_t *e) {
	int rc;
	uv__worker_t *worker = NULL;
	uv__executor_channel_t *channel = NULL;

	if (e == NULL) abort();

	uv_log(1, "uv__executor_new_worker: Entry\n");

	/* Allocate worker and channel. */
	worker = (uv__worker_t *) uv__malloc(sizeof(*worker));
	if (worker == NULL) abort();

	channel = uv__executor_channel_create(&e->manager.async);
  if (channel == NULL) abort();

	uv_log(1, "uv__executor_new_worker: worker %p channel %p\n", worker, channel);

	/* Update manager. Safe because:
   *    1. worker has not yet started (so manager is blocked in uv_run)
   *  or 
   *    2. manager is calling us from uv__manager_timer and has already saved the mutex and started the hangman. */
	e->manager.channel = channel;
	e->manager.last_observed_work = NULL;

	e->worker = worker;
	rc = launch_worker(worker, channel);
	return rc;
}

/***************
 * uv__manager_t
 ***************/

static int uv__manager_init (uv__manager_t *mgr) {
	int rc;

	if (mgr == NULL) abort();

	mgr->tid = -1;

	rc = uv_loop_init(&mgr->loop);
	if (rc) abort();

	rc = uv_async_init(&mgr->loop, &mgr->async, uv__manager_async);
	if (rc) abort();

	rc = uv_timer_init(&mgr->loop, &mgr->timer);
	if (rc) abort();

	mgr->closing = 0;
	rc = uv_sem_init(&mgr->final_send_sent, 0);
	if (rc) abort();

	mgr->channel = NULL;
	mgr->last_observed_work = NULL;
	return 0;
}

int launch_manager (uv__manager_t *mgr) {
	int rc;
	
	if (mgr == NULL) abort();

	rc = uv__manager_init(mgr);
	if (rc) abort();

	rc = uv_thread_create(&mgr->tid, manager, mgr);
	uv_log(1, "launch_manager: launched %lu (rc %i)\n", mgr->tid, rc);
	return rc;
}

void manager (void *arg) {
	int rc;
	uv__executor_t *executor = NULL;
	uv__manager_t *self = NULL;

	self = (uv__manager_t *) arg;
	assert(self != NULL);

	rc = uv_run(&self->loop, UV_RUN_DEFAULT);
	if (rc) abort();
	uv_log(1, "manager: uv_run returned, guess we're done\n");

	uv_log(1, "manager: cleaning up my worker\n");
  executor = container_of(self, uv__executor_t, manager);
	launch_hangman(executor->worker, NULL, NULL);

	uv_log(1, "manager: closing my loop\n");
	rc = uv_loop_close(&self->loop);
	if (rc) abort();

	uv_log(1, "manager: Farewell\n");
	return;
}

void uv__manager_async (uv_async_t *handle) {
	uv__manager_t *self = NULL;
	int rc;
	uint64_t timeout_ms = 0;
  int valid_wakeup = 0;

	if (handle == NULL) abort();
  self = container_of(handle, uv__manager_t, async);

	uv_log(1, "uv__manager_async: got async\n");

	/* async comes from three places:
   *  1. Worker, work done
   *  2. Worker, new work
   *  3. Executor, closing
   * Track with valid_wakeup for paranoia. */

	uv_mutex_lock(&self->channel->mutex);
		/* Did the worker wake us up? */

		if (self->channel->curr_work == NULL) {
			/* 1. Work done. */
			valid_wakeup = 1;
			uv_log(1, "uv__manager_async: worker finished work, stopping timer\n");
			rc = uv_timer_stop(&self->timer);
			if (rc) abort();
		}
		else {
			if (self->channel->curr_work != self->last_observed_work) {
				/* 2. New work */
				valid_wakeup = 1;
				uv_log(1, "uv__manager_async: worker found new work. It is working on %p, I last saw %p\n", self->channel->curr_work, self->last_observed_work);

				/* Reset the timer. */
				uv_log(1, "uv__manager_async: Starting a timer for 500 ms\n");
				rc = uv_timer_stop(&self->timer);
				if (rc) abort();

				/* TODO Choose timeout somehow, e.g. from input or from env var. */
				timeout_ms = 500; /* 0.5 seconds */
				rc = uv_timer_start(&self->timer, uv__manager_timer, timeout_ms, 0);
				if (rc) abort();
			}
			else {
				uv_log(1, "uv__manager_async: worker is still working on the same work, it must not have woken me\n");
			}
		}
		self->last_observed_work = self->channel->curr_work; /* Update observation. */
	uv_mutex_unlock(&self->channel->mutex);

	if (self->closing) {
		/* 3. Executor, closing */
		valid_wakeup = 1;
		uv_log(1, "uv__manager_async: closing, so closing my handles\n");
		uv_sem_wait(&self->final_send_sent);
		uv_close((uv_handle_t *) &self->async, NULL);
		(void) uv_timer_stop(&self->timer);
		uv_close((uv_handle_t *) &self->timer, NULL);
	}

	if (!valid_wakeup)
		uv_log(1, "uv__manager_async: spurious wakeup?\n"); /* I see these occasionally. Not sure of the source. Maybe uv_async_send might spuriously wake us up? */

	return;
}

void uv__manager_timer (uv_timer_t *handle) {
	int rc;
  uv__manager_t *self = NULL;
  uv__executor_t *executor = NULL;
	uv_mutex_t *channel_mutex = NULL;
	uint64_t grace_period_ms = 0;
	void *killed_dat = NULL;

	if (handle == NULL) abort();
  self = container_of(handle, uv__manager_t, timer);
  executor = container_of(self, uv__executor_t, manager);
	uv_log(1, "uv__manager_timer: got timer\n");

	/* If we abort the Worker, we create a new channel. Remember the old channel's mutex's address. */
	channel_mutex = (&self->channel->mutex);

	uv_mutex_lock(channel_mutex);
		if (self->last_observed_work == self->channel->curr_work) {
			struct uv__work *work = self->last_observed_work;
			/* Still working on same work as last time, a legitimate timeout. */
			uv_log(1, "uv__manager_timer: got timer, worker still working on same work.\n");

			/* Inform owner that his task has timed out. See what response he wants. */
			grace_period_ms = 0;
			killed_dat = NULL;
			if (work->timed_out) {
				uv_log(1, "uv__manager_timer: Calling timed_out cb to see what to do\n");
				grace_period_ms = work->timed_out(work, &killed_dat);
			}
			uv_log(1, "uv__manager_timer: grace_period_ms %llu\n", grace_period_ms);

			if (grace_period_ms) {
				/* Reset timer for the grace period. */
				uv_log(1, "uv__manager_timer: timed_out cb requested grace period of %llu ms. Resetting timer.\n", grace_period_ms);
				rc = uv_timer_stop(&self->timer);
				if (rc) abort();
				rc = uv_timer_start(&self->timer, uv__manager_timer, grace_period_ms, 0);
				if (rc) abort();
			}
			else {
				/* Owner agrees we can time out the request. Hangman. */
				uv_log(1, "uv__manager_timer: No grace period requested. killed_dat %p. Launching hangman.\n", killed_dat);
		    work->state_timed_out = 1; /* Signal for uv__work_done. */
				self->channel->timed_out = 1; /* Signal for worker. */
				/* Hangman gets the old worker, so it can clean up the associated memory allocated in uv__executor_new_worker. */
				launch_hangman(executor->worker, work->killed, killed_dat);
		
				/* Since we are still holding the channel mutex, we are guaranteed that the worker won't call uv_async_send again. Safe to give a new Worker this async without confusion. */
				self->last_observed_work = NULL;

				uv_log(1, "uv__manager_timer: Creating new worker\n");
				rc = uv__executor_new_worker(executor);
				uv_log(1, "uv__manager_timer: New worker: %x\n", executor->worker->tid);
				if (rc) abort();
			}
		}
		else {
			/* Timer expired but work has changed. NBD. */
			uv_log(1, "uv__manager_timer: got timer, but it looks like the worker finished the work already. Doing nothing.\n");
		}
	uv_mutex_unlock(channel_mutex);

	return;
}

/***************
 * uv__worker_t
 ***************/

int launch_worker (uv__worker_t *worker_, uv__executor_channel_t *channel) {
	int rc;

	if (worker_ == NULL) abort();
	if (channel == NULL) abort();

	worker_->channel = channel;
	rc = uv_thread_create(&worker_->tid, worker, worker_);
	uv_log(1, "launch_worker: launched %lu (rc %i)\n", worker_->tid, rc);
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
			uv_log(1, "worker: Waiting for work\n");
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
		uv_log(1, "worker: Got work %p\n", w);
    
		uv_mutex_lock(&self->channel->mutex);
			if (self->channel->timed_out) {
				/* If we've been timed out (??), re-queue work and return. */
				uv_log(1, "worker: Timed out before starting work??\n");
				uv_mutex_unlock(&self->channel->mutex);
				post(&w->wq);
			}

			/* Tell manager we have a new task */
			uv_log(1, "worker: Telling manager we have new work\n");
			self->channel->curr_work = w;
			self->channel->timed_out = 0;
			uv_async_send(self->channel->async);
		uv_mutex_unlock(&self->channel->mutex);

		/* Do the work */
    w->work(w);

		uv_log(1, "worker: Finished work\n");

		uv_mutex_lock(&self->channel->mutex);
			/* Check if we timed out. */
			if (self->channel->timed_out) {
				/* There's a hangman out for our blood.
				 * He will clean up our corpse. */
				uv_log(1, "worker: Timed out, returning\n");
				uv_mutex_unlock(&self->channel->mutex);
				return;
			}

			/* Tell manager we finished our task. */
			uv_log(1, "worker: Telling manager we finished work\n");
			self->channel->curr_work = NULL;
			uv_async_send(self->channel->async);
		uv_mutex_unlock(&self->channel->mutex);

		/* Prepare for next task. */
		uv_log(1, "worker: Signaling loop that task %p is done.\n", w);
		queue_completed_work(w);
  }

	uv_log(1, "worker: Farewell\n");
	return;
}

/***************
 * uv__hangman_t
 ***************/

void launch_hangman (uv__worker_t *victim,
                     void (*killed_cb)(void *dat),
										 void *killed_dat) {
	int rc;
	uv__hangman_t *hangman_ = NULL;

	hangman_ = uv__malloc(sizeof(*hangman_));
	if (hangman_ == NULL) abort();

	uv_log(1, "launch_hangman: victim %p killed_dat %p\n", victim, killed_dat);

	hangman_->victim = victim;
	hangman_->killed_cb = killed_cb;
	hangman_->killed_dat = killed_dat;

	rc = uv_sem_init(&hangman_->done_with_w, 0);
	if (rc) abort();

	rc = uv_thread_create(&hangman_->tid, hangman, hangman_);
	if (rc) abort();

  uv_log(1, "launch_hangman: waiting for hangman to be done touching w\n");
	uv_sem_wait(&hangman_->done_with_w);

	return;
}

void hangman (void *h) {
	int rc;
	uv__hangman_t *hangman_ = NULL;

	uv_log(1, "hangman: Entry\n");
	hangman_ = (uv__hangman_t *) h;
	if (hangman_ == NULL) abort();

	/* Caller doesn't need to join() on us. */
	rc = uv_thread_detach(&hangman_->tid);
	if (rc) abort();

	if (hangman_->victim != NULL) {
    /* If the victim was doing work, add it to the done queue so its done_cb can be called. */
		struct uv__work *w = hangman_->victim->channel->curr_work;

		if (w != NULL) {
			/* Prepare for next task. */
			uv_log(1, "hangman: Signaling loop that task %p is done.\n", w);
			w->state_timed_out = 1;
			queue_completed_work(w);
		}

    /* Signal launch_hangman that it can return, making w invalid. */
		uv_sem_post(&hangman_->done_with_w);
		/* At this point w->loop's done_cb has fired (uv__work_done), so we can no longer safely access the uv_req_t/struct uv__work associated with the victim.
		 * This is why we use a void *dat set by the timed_out_cb. */
		hangman_->victim->channel->curr_work = NULL; /* Avoid temptation. */

		/* Cancel the victim.
     * It might already have finished its task, seen channel->timed_out, and returned, so ignore the uv_thread_cancel rc. */
		uv_log(1, "hangman: Cancel'ing victim %lld\n", hangman_->victim->tid);
		uv_thread_cancel(&hangman_->victim->tid);

		/* Join the victim. */
		uv_log(1, "hangman: Joining victim worker %lld\n", hangman_->victim->tid);
		rc = uv_thread_join(&hangman_->victim->tid);
		if (rc) abort();

		/* Call the killed_cb if we have one. */
		if (hangman_->killed_cb != NULL) {
			uv_log(1, "hangman: Calling killed_cb with %p\n", hangman_->killed_dat);
			hangman_->killed_cb(hangman_->killed_dat); /* NOT INVOKED FROM MAIN THREAD. */
		}
		
		/* Clean up */
		uv_log(1, "hangman: Releasing victim's channel and memory\n");
		uv__executor_channel_destroy(hangman_->victim->channel);
		uv__free(hangman_->victim);
	}

	/* Clean up self */
	uv__free(hangman_);

	uv_log(1, "hangman: Farewell\n");
	return;
}

/***************
 * uv__executor_channel_t
 ***************/

uv__executor_channel_t * uv__executor_channel_create (uv_async_t *async) {
	uv__executor_channel_t *ret = NULL;

	if (async == NULL) abort();
	ret = uv__malloc(sizeof(*ret));
	if (ret == NULL) abort();

	ret->async = async;
	if (uv_mutex_init(&ret->mutex))
		abort();

	ret->curr_work = NULL;
	ret->timed_out = 0;

	return ret;
}

void uv__executor_channel_destroy (uv__executor_channel_t *channel) {
	if (channel == NULL) abort();

	uv_mutex_destroy(&channel->mutex);
	uv__free(channel);
}

// TODO Need to uv_mutex_destroy and uv_sem_destroy everything.
