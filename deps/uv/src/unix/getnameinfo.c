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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uv.h"
#include "internal.h"


static void uv__getnameinfo_work(struct uv__work* w) {
  uv_getnameinfo_t* req;
  int err;
  socklen_t salen;

  req = container_of(w, uv_getnameinfo_t, work_req);

  if (req->buf->storage.ss_family == AF_INET)
    salen = sizeof(struct sockaddr_in);
  else if (req->buf->storage.ss_family == AF_INET6)
    salen = sizeof(struct sockaddr_in6);
  else
    abort();

  err = getnameinfo((struct sockaddr*) &req->buf->storage,
                    salen,
                    req->buf->host,
                    sizeof(req->buf->host),
                    req->buf->service,
                    sizeof(req->buf->service),
                    req->flags);
  req->retcode = uv__getaddrinfo_translate_error(err);
}

static uint64_t uv__getnameinfo_timed_out (struct uv__work* w, void **dat) {
  uv_getnameinfo_t* req;

  req = container_of(w, uv_getnameinfo_t, work_req);

  dprintf(2, "uv__getnameinfo_timed_out: work %p dat %p timed out\n", w, dat);

	/* Propagate to uv__getnameinfo_done. */
	req->retcode = -ETIMEDOUT;

	/* getnameinfo is read-only.
	 * However, the runaway uv__getnameinfo_work may continue to modify the buf,
	 * so we have to free the memory once it is killed. */
	*dat = req->buf;

  /* Tell threadpool to abort the Task. */
	return 0;
}

static void uv__getnameinfo_done(struct uv__work* w, int status) {
  uv_getnameinfo_t* req;
  char* host;
  char* service;

  req = container_of(w, uv_getnameinfo_t, work_req);
  uv__req_unregister(req->loop, req);
  host = service = NULL;

  if (status == -ECANCELED) {
    assert(req->retcode == 0);
    req->retcode = UV_EAI_CANCELED;
  }
	else if (status == -ETIMEDOUT) {
		assert(req->retcode == -ETIMEDOUT);
  }
  else if (req->retcode == 0) {
		memcpy(req->host, req->buf->host, sizeof(req->buf->host));
		host = req->host;

		memcpy(req->service, req->buf->service, sizeof(req->buf->service));
		service = req->service;
  }

  /* If we aren't timed out, we have to clean up our buf. */
	if (req->retcode != -ETIMEDOUT)
		uv__free(req->buf);

  if (req->getnameinfo_cb)
    req->getnameinfo_cb(req, req->retcode, host, service);
}

static void uv__getnameinfo_killed(void *dat) {
	uv__getnameinfo_buf_t *buf = (uv__getnameinfo_buf_t *) dat;

	/* Resource management: Free the buf. */
	dprintf(2, "uv__getnameinfo_killed: Freeing %p\n", dat);
	uv__free(buf);
}

/*
* Entry point for getnameinfo
* return 0 if a callback will be made
* return error code if validation fails
*/
int uv_getnameinfo(uv_loop_t* loop,
                   uv_getnameinfo_t* req,
                   uv_getnameinfo_cb getnameinfo_cb,
                   const struct sockaddr* addr,
                   int flags) {
	dprintf(2, "uv_getaddrinfo: entry\n");

  if (req == NULL || addr == NULL)
    return UV_EINVAL;
	
	/* We have to allocate an internal buffer for uv__getnameinfo_work to modify.
	 * That way, if we time it out, it's safe to uv__getnameinfo_done without worrying about memory corruption later. */
	req->buf = (uv__getnameinfo_buf_t *) uv__malloc(sizeof(*req->buf));
	if (req->buf == NULL)
		return UV_ENOMEM;
	memset(req->buf, 0, sizeof(*req->buf));

  if (addr->sa_family == AF_INET) {
    memcpy(&req->buf->storage,
           addr,
           sizeof(struct sockaddr_in));
  } else if (addr->sa_family == AF_INET6) {
    memcpy(&req->buf->storage,
           addr,
           sizeof(struct sockaddr_in6));
  } else {
    return UV_EINVAL;
  }

  uv__req_init(loop, (uv_req_t*)req, UV_GETNAMEINFO);

  req->getnameinfo_cb = getnameinfo_cb;
  req->flags = flags;
  req->type = UV_GETNAMEINFO;
  req->loop = loop;
  req->retcode = 0;

  if (getnameinfo_cb) {
    uv__work_submit(loop,
                    &req->work_req,
                    uv__getnameinfo_work,
                    uv__getnameinfo_timed_out,
                    uv__getnameinfo_done,
                    uv__getnameinfo_killed);
    return 0;
  } else {
		abort(); /* Node does not offer a synchronous API, make sure this never happens. */
    uv__getnameinfo_work(&req->work_req);
    uv__getnameinfo_done(&req->work_req, 0);
    return req->retcode;
  }
}
