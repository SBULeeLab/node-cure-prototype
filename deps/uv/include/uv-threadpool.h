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

struct uv__work {
  void (*work)(struct uv__work *w); /* Set to NULL when work() returns. */
	uint64_t (*timed_out)(struct uv__work *w, void **dat); /* See uv_timed_out_cb. */
  void (*done)(struct uv__work *w, int status);
	void (*killed)(void *dat); /* See uv_killed_cb. */
  struct uv_loop_s* loop;
  void* wq[2]; /* QUEUE_EMPTY(&wq) when work() is active. */

	int prio; /* uv__work_submit_prio */

	/* FOR DEBUGGING.
	 * Managed by the default uv__queue_X APIs for callers of uv_queue_work. */
	int state_queued;
	int state_assigned;
	int state_timed_out;
	int state_done;
	int state_canceled;
};

#endif /* UV_THREADPOOL_H_ */
