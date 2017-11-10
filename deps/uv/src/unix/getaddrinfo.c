/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

/* Expose glibc-specific EAI_* error codes. Needs to be defined before we
 * include any headers.
 */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <stddef.h> /* NULL */
#include <stdlib.h>
#include <string.h>

/* EAI_* constants. */
#include <netdb.h>

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

int uv__getaddrinfo_translate_error(int sys_err) {
  switch (sys_err) {
  case 0: return 0;
#if defined(EAI_ADDRFAMILY)
  case EAI_ADDRFAMILY: return UV_EAI_ADDRFAMILY;
#endif
#if defined(EAI_AGAIN)
  case EAI_AGAIN: return UV_EAI_AGAIN;
#endif
#if defined(EAI_BADFLAGS)
  case EAI_BADFLAGS: return UV_EAI_BADFLAGS;
#endif
#if defined(EAI_BADHINTS)
  case EAI_BADHINTS: return UV_EAI_BADHINTS;
#endif
#if defined(EAI_CANCELED)
  case EAI_CANCELED: return UV_EAI_CANCELED;
#endif
#if defined(EAI_FAIL)
  case EAI_FAIL: return UV_EAI_FAIL;
#endif
#if defined(EAI_FAMILY)
  case EAI_FAMILY: return UV_EAI_FAMILY;
#endif
#if defined(EAI_MEMORY)
  case EAI_MEMORY: return UV_EAI_MEMORY;
#endif
#if defined(EAI_NODATA)
  case EAI_NODATA: return UV_EAI_NODATA;
#endif
#if defined(EAI_NONAME)
# if !defined(EAI_NODATA) || EAI_NODATA != EAI_NONAME
  case EAI_NONAME: return UV_EAI_NONAME;
# endif
#endif
#if defined(EAI_OVERFLOW)
  case EAI_OVERFLOW: return UV_EAI_OVERFLOW;
#endif
#if defined(EAI_PROTOCOL)
  case EAI_PROTOCOL: return UV_EAI_PROTOCOL;
#endif
#if defined(EAI_SERVICE)
  case EAI_SERVICE: return UV_EAI_SERVICE;
#endif
#if defined(EAI_SOCKTYPE)
  case EAI_SOCKTYPE: return UV_EAI_SOCKTYPE;
#endif
#if defined(EAI_SYSTEM)
  case EAI_SYSTEM: return -errno;
#endif
  }
  assert(!"unknown EAI_* error code");
  abort();
  return 0;  /* Pacify compiler. */
}


static void uv__getaddrinfo_work(struct uv__work* w) {
  uv_getaddrinfo_t* req;
  int err;

  req = container_of(w, uv_getaddrinfo_t, work_req);
  /* getaddrinfo is not AC-safe: AC-Unsafe lock corrupt mem fd. */
	mark_not_cancelable();
  err = getaddrinfo(req->buf->hostname, req->buf->service, &req->buf->hints, &req->buf->addrinfo);
	mark_cancelable();
  req->retcode = uv__getaddrinfo_translate_error(err);
}

static uint64_t uv__getaddrinfo_timed_out (struct uv__work* w, void **dat) {
  uv_getaddrinfo_t* req;

  req = container_of(w, uv_getaddrinfo_t, work_req);

  dprintf(2, "uv__getaddrinfo_timed_out: work %p dat %p timed out\n", w, dat);

	/* Propagate to uv__getaddrinfo_done. */
	req->retcode = -ETIMEDOUT;

	/* getaddrinfo is read-only.
	 * However, the runaway uv__getaddrinfo_work may continue to modify the buf,
	 * so we have to free the memory once it is killed. */
	*dat = req->buf;

	/* Resource management policy:
	 *   TODO We don't do anything here.
	 *   Recommendation:
	 *     A DNS timeout is probably due to a flaky network.
	 *     The failure is likely not permanent. 
	 *     Thus a temporal blacklist seems appropriate: time out requests to the same resource for the next X minutes. */

  /* Tell threadpool to abort the Task. */
	return 0;
}

static void uv__getaddrinfo_done(struct uv__work* w, int status) {
  uv_getaddrinfo_t* req;

  req = container_of(w, uv_getaddrinfo_t, work_req);
  uv__req_unregister(req->loop, req);

  if (status == -ECANCELED) {
    assert(req->retcode == 0);
    req->retcode = UV_EAI_CANCELED;
  }
	else if (status == -ETIMEDOUT) {
		assert(req->retcode == -ETIMEDOUT);
	}
	else {
		req->addrinfo = req->buf->addrinfo;
	}

  /* If we aren't timed out, we have to clean up our buf. */
	if (req->retcode != -ETIMEDOUT)
		uv__free(req->buf);

  if (req->cb)
    req->cb(req, req->retcode, req->addrinfo);
}

static void uv__getaddrinfo_killed(void *dat) {
	uv__getaddrinfo_buf_t *buf = (uv__getaddrinfo_buf_t *) dat;

	/* Resource management: Free the buf. */
	dprintf(2, "uv__getaddrinfo_killed: Freeing %p\n", dat);
	uv__free(buf);
}


int uv_getaddrinfo(uv_loop_t* loop,
                   uv_getaddrinfo_t* req,
                   uv_getaddrinfo_cb cb,
                   const char* hostname,
                   const char* service,
                   const struct addrinfo* hints) {
  size_t hostname_len;
  size_t service_len;

	dprintf(2, "uv_getaddrinfo: entry\n");

  if (req == NULL || (hostname == NULL && service == NULL))
    return -EINVAL;

  hostname_len = hostname ? strlen(hostname) + 1 : 0;
  service_len = service ? strlen(service) + 1 : 0;
	if (NI_MAXHOST <= hostname_len || NI_MAXSERV <= service_len)
		return -EINVAL;

	req->buf = (uv__getaddrinfo_buf_t *) uv__malloc(sizeof(*req->buf));
  if (req->buf == NULL)
    return -ENOMEM;
	memset(req->buf, 0, sizeof(*req->buf));

  uv__req_init(loop, req, UV_GETADDRINFO);
  req->loop = loop;
  req->cb = cb;
  req->addrinfo = NULL;
  req->retcode = 0;

  if (hints)
    memcpy(&req->buf->hints, hints, sizeof(*hints));
  if (service)
    strncpy(req->buf->service, service, service_len);
  if (hostname)
    strncpy(req->buf->hostname, hostname, hostname_len);

	req->buf->service[service_len] = '\0';
	req->buf->hostname[hostname_len] = '\0';
	req->buf->addrinfo = NULL;

  if (cb) {
    uv__work_submit(loop,
                    &req->work_req,
                    uv__getaddrinfo_work,
                    uv__getaddrinfo_timed_out,
                    uv__getaddrinfo_done,
                    uv__getaddrinfo_killed);
    return 0;
  } else {
		abort(); /* Node does not offer a synchronous API, make sure this never happens. */
    uv__getaddrinfo_work(&req->work_req);
    uv__getaddrinfo_done(&req->work_req, 0);
    return req->retcode;
  }
}


void uv_freeaddrinfo(struct addrinfo* ai) {
  if (ai)
    freeaddrinfo(ai);
}
