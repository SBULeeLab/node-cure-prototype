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

/* Caveat emptor: this file deviates from the libuv convention of returning
 * negated errno codes. Most uv_fs_*() functions map directly to the system
 * call of the same name. For more complex wrappers, it's easier to just
 * return -1 with errno set. The dispatcher in uv__fs_work() takes care of
 * getting the errno to the right place (req->result or as the return value.)
 */

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h> /* PATH_MAX */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <poll.h>

#if defined(__DragonFly__)        ||                                      \
    defined(__FreeBSD__)          ||                                      \
    defined(__FreeBSD_kernel_)    ||                                      \
    defined(__OpenBSD__)          ||                                      \
    defined(__NetBSD__)
# define HAVE_PREADV 1
#else
# define HAVE_PREADV 0
#endif

#if defined(__linux__) || defined(__sun)
# include <sys/sendfile.h>
#endif

#if defined(__APPLE__)
# include <copyfile.h>
#endif

#include "../uthash/include/uthash.h"

/**
 * Timeout resource management policy:
 *
 * We manage four structures:
 *  1. Slow Paths (we use a hash of the realpath())
 *  2. Slow FDs
 *  3. Map of fds to inodes
 *  4. Slow Inodes
 *  [5. Slow Devices] Not tracked, but could, defend against slow network mounts, policy decision.
 *
 * These tables are used as follows:
 *  1. When we are asked to open, we obtain realpath and return a timeout if the path is in Slow Paths.
 *     If the open times out, we add the realpath to Slow Paths.
 *     If the open succeeds, we stat to obtain the inode number and add it to the fds-to-inodes map.
 *     If the stat times out, we add the fd to Slow FDs.
 *
 *  2. When we close, we remove the fd from Slow FDs.
 *
 *  3. When we do I/O on an fd, we consult the Slow FDs table and return a timeout if present.
 *     If the I/O times out, we obtain the inode from the fds-to-inodes map and add it to Slow Inodes.
 *
 * The Slow X structures are permanent blacklists.
 * This is a policy decision.
 *
 * Relevant thoughts:
 *    - If a write times out, we can't allow access until the write is killed to ensure coherent state
 *      (thus ensuring the runaway write won't have more effects).
 *    - It's conceivable that the underlying resource was slow because of a network hiccup,
 *      in which case the blacklist should be temporal once the write-blocking has expired.
 *    - realpath, open, and stat can all block, e.g. due to a slow symlink, so these should all be under a timeout.
 *    - See research journal for 7 Nov. about the use of pthread_cancel with these syscalls (via libc).
 *      Basically, we'll wrap close() in a "don't cancel me", but everything else we'll let slide,
 *      carrying the risk of leaked fds.
 */

 /* NB These struct field names are embedded in macros. Don't change them. */ 

typedef struct slow_rph_s {
  unsigned rph; /* rph: realpath hash */
  UT_hash_handle hh;
} slow_rph_t;

typedef struct slow_fd_s {
  uv_file fd;
  UT_hash_handle hh;
} slow_fd_t;

typedef struct slow_ino_s {
  ino_t ino;
  UT_hash_handle hh;
} slow_ino_t;

typedef struct fd2ino_s {
  uv_file fd; /* Key. */
  ino_t ino; /* Value. */
  UT_hash_handle hh;
} fd2ino_t;

static uv_once_t slow_resources_init_once = UV_ONCE_INIT;

static slow_fd_t *slow_rph_table = NULL; 
static uv_mutex_t slow_rph_lock;

static slow_fd_t *slow_fd_table = NULL; 
static uv_mutex_t slow_fd_lock;

static slow_ino_t *slow_ino_table = NULL; 
static uv_mutex_t slow_ino_lock;

static fd2ino_t *fd2ino_table = NULL;  /* Implemented as a hash table keyed by fd, with entries an <fd, ino> pair. */
static uv_mutex_t fd2ino_lock;

static void slow_resources_init (void) {
  static int initialized = 0;
  if (initialized)
		return;
	else
		initialized = 1;

	dprintf(2, "slow_resources_init: Initializing locks\n");
	if (uv_mutex_init(&slow_rph_lock))
		abort();
	if (uv_mutex_init(&slow_fd_lock))
		abort();
	if (uv_mutex_init(&slow_ino_lock))
		abort();
	if (uv_mutex_init(&fd2ino_lock))
		abort();
}

typedef enum {
  SLOW_RESOURCE_REALPATH,
  SLOW_RESOURCE_FD,
  SLOW_RESOURCE_INODE
} slow_resource_t;

/* Macros to access Slow Resources -- Slow RPH, Slow FD, Slow Inode.
 * Caller should hold the appropriate lock. */

/* TODO Lots of duplicated code here, could replace with fancy macros like in V8's execution.cc. But hard to debug. */

/* RPH */

/* unsigned* */
#define FIND_RPH(rphP, out)                                                   \
    HASH_FIND(hh, slow_rph_table, rph, sizeof(unsigned), out)

/* slow_rph_t* */
#define ADD_RPH(add)                                                          \
    HASH_ADD(hh, slow_rph_table, rph, sizeof(unsigned), add)

/* slow_rph_t*, slow_rph_t* */
#define REPLACE_RPH(add, replaced)                                            \
    HASH_REPLACE(hh, slow_rph_table, rph, sizeof(unsigned), add, replaced)

/* FD */
#define FIND_FD(fd, out)                                                      \
    HASH_FIND(hh, slow_fd_table, fd, sizeof(uv_file), out)

#define ADD_FD(add)                                                           \
    HASH_ADD(hh, slow_fd_table, fd, sizeof(uv_file), add)

#define REPLACE_FD(add, replaced)                                             \
    HASH_REPLACE(hh, slow_fd_table, fd, sizeof(uv_file), add, replaced)

/* Inode */
#define FIND_INO(ino, out)                                                    \
    HASH_FIND(hh, slow_ino_table, ino, sizeof(ino_t), out)

#define ADD_INO(add)                                                          \
    HASH_ADD(hh, slow_ino_table, ino, sizeof(ino_t), add)

#define REPLACE_INO(add, replaced)                                            \
    HASH_REPLACE(hh, slow_ino_table, ino, sizeof(ino_t), add, replaced)

/* Macros to access the fd2inode map. */
#define FIND_FD2INO(fd, out)                                                    \
    HASH_FIND(hh, fd2ino_table, fd , sizeof(uv_file), out)

#define ADD_FD2INO(add)                                                          \
    HASH_ADD(hh, fd2ino_table, fd, sizeof(uv_file), add)

#define REPLACE_FD2INO(add, replaced)                                            \
    HASH_REPLACE(hh, fd2ino_table, fd, sizeof(uv_file), add, replaced)

/* Thread-safe APIs for the Slow X hashtables. */

static slow_rph_t * slow_rph_find (unsigned rph) {
  slow_rph_t *result;

	uv_mutex_lock(&slow_rph_lock);
  FIND_RPH(&rph, result);
	uv_mutex_unlock(&slow_rph_lock);

	if (result != NULL)
		dprintf(2, "slow_rph_find: rph %u is not slow\n", rph);
  else
		dprintf(2, "slow_rph_find: rph %u is slow\n", rph);

	return result;
}

static int rph_is_slow (unsigned rph) {
	return (slow_rph_find(rph) != NULL);
}

static slow_rph_t * slow_rph_add (unsigned rph) {
  slow_rph_t *slow_rph;

	uv_mutex_lock(&slow_rph_lock);
	if (rph_is_slow(rph))
		return;

  slow_rph = (slow_rph_t *) uv__malloc(sizeof(*slow_rph));
	if (slow_rph == NULL)
		abort();
	slow_rph->rph = rph;

  ADD_RPH(slow_rph);
	uv_mutex_unlock(&slow_rph_lock);

	return slow_rph;
}

/* Caller must uv__free the returned value. */
static slow_rph_t * slow_rph_replace (unsigned rph) {
  slow_rph_t *slow_rph_old, *slow_rph_new;

	uv_mutex_lock(&slow_rph_lock);
	if (rph_is_slow(rph))
		return;

  slow_rph_new = (slow_rph_t *) uv__malloc(sizeof(*slow_rph_new));
	if (slow_rph_new == NULL)
		abort();
	slow_rph_new->rph = rph;

  REPLACE_RPH(slow_rph_new, slow_rph_old);
	uv_mutex_unlock(&slow_rph_lock);

	return slow_rph_old;
}

static slow_fd_t * slow_fd_find (uv_file fd) {
  slow_fd_t *result;

	uv_mutex_lock(&slow_fd_lock);
  FIND_FD(&fd, result);
	uv_mutex_unlock(&slow_fd_lock);

	if (result != NULL)
		dprintf(2, "slow_fd_find: fd %u is not slow\n", fd);
  else
		dprintf(2, "slow_fd_find: fd %u is slow\n", fd);

	return result;
}

static int fd_is_slow (uv_file fd) {
	return (slow_fd_find(fd) != NULL);
}

static slow_fd_t * slow_fd_add (uv_file fd) {
  slow_fd_t *slow_fd;

	uv_mutex_lock(&slow_fd_lock);
	if (fd_is_slow(fd))
		return;

  slow_fd = (slow_fd_t *) uv__malloc(sizeof(*slow_fd));
	if (slow_fd == NULL)
		abort();
	slow_fd->fd = fd;

  ADD_FD(slow_fd);
	uv_mutex_unlock(&slow_fd_lock);

	return slow_fd;
}

/* Caller must uv__free the returned value. */
static slow_fd_t * slow_fd_replace (uv_file fd) {
  slow_fd_t *slow_fd_old, *slow_fd_new;

	uv_mutex_lock(&slow_fd_lock);
	if (fd_is_slow(fd))
		return;

  slow_fd_new = (slow_fd_t *) uv__malloc(sizeof(*slow_fd_new));
	if (slow_fd_new == NULL)
		abort();
	slow_fd_new->fd = fd;

  REPLACE_FD(slow_fd_new, slow_fd_old);
	uv_mutex_unlock(&slow_fd_lock);

	return slow_fd_old;
}

static void slow_fd_delete (uv_file fd) {
	slow_fd_t *slow_fd;

	uv_mutex_lock(&slow_fd_lock);
	slow_fd = slow_fd_find(fd);
  if (slow_fd != NULL) {
		HASH_DEL(slow_fd_table, slow_fd);
		uv__free(slow_fd);
	}
	uv_mutex_unlock(&slow_fd_lock);
}

static slow_ino_t * slow_ino_find (unsigned ino) {
  slow_ino_t *result;

	uv_mutex_lock(&slow_ino_lock);
  FIND_INO(&ino, result);
	uv_mutex_unlock(&slow_ino_lock);

	if (result != NULL)
		dprintf(2, "slow_ino_find: ino %u is not slow\n", ino);
  else
		dprintf(2, "slow_ino_find: ino %u is slow\n", ino);

	return result;
}

static int ino_is_slow (unsigned ino) {
	return (slow_ino_find(ino) != NULL);
}

static slow_ino_t * slow_ino_add (unsigned ino) {
  slow_ino_t *slow_ino;

	uv_mutex_lock(&slow_ino_lock);
	if (ino_is_slow(ino))
		return;

  slow_ino = (slow_ino_t *) uv__malloc(sizeof(*slow_ino));
	if (slow_ino == NULL)
		abort();
	slow_ino->ino = ino;

  ADD_INO(slow_ino);
	uv_mutex_unlock(&slow_ino_lock);

	return slow_ino;
}

/* Caller must uv__free the returned value. */
static slow_ino_t * slow_ino_replace (unsigned ino) {
  slow_ino_t *slow_ino_old, *slow_ino_new;

	uv_mutex_lock(&slow_ino_lock);
	if (ino_is_slow(ino))
		return;

  slow_ino_new = (slow_ino_t *) uv__malloc(sizeof(*slow_ino_new));
	if (slow_ino_new == NULL)
		abort();
	slow_ino_new->ino = ino;

  REPLACE_INO(slow_ino_new, slow_ino_old);
	uv_mutex_unlock(&slow_ino_lock);

	return slow_ino_old;
}

static fd2ino_t * fd2ino_find (uv_file fd) {
  fd2ino_t *result;

	uv_mutex_lock(&fd2ino_lock);
  FIND_FD2INO(&fd, result);
	uv_mutex_unlock(&fd2ino_lock);

	if (result != NULL)
		dprintf(2, "fd2ino_find: fd %u is not slow\n", fd);
  else
		dprintf(2, "fd2ino_find: fd %u is slow\n", fd);

	return result;
}

static int fd2ino_known (uv_file fd) {
	return (fd2ino_find(fd) != NULL);
}

static fd2ino_t * fd2ino_add (uv_file fd, ino_t ino) {
  fd2ino_t *fd2ino;

  dprintf(2, "fd2ino_add: %d -> %llu\n", fd, ino);

	uv_mutex_lock(&fd2ino_lock);
	if (fd2ino_known(fd))
		abort(); /* One fd table per process, so should open() and get an fd we already know. */

  fd2ino = (fd2ino_t *) uv__malloc(sizeof(*fd2ino));
	if (fd2ino == NULL)
		abort();
	fd2ino->fd = fd;
	fd2ino->ino = ino;

  ADD_FD2INO(fd2ino);
	uv_mutex_unlock(&fd2ino_lock);

	return fd2ino;
}

/* Caller must uv__free the returned value. */
static fd2ino_t * fd2ino_replace (uv_file fd, ino_t ino) {
  fd2ino_t *fd2ino_old, *fd2ino_new;

	uv_mutex_lock(&fd2ino_lock);
	if (fd_is_slow(fd))
		return;

  fd2ino_new = (fd2ino_t *) uv__malloc(sizeof(*fd2ino_new));
	if (fd2ino_new == NULL)
		abort();
	fd2ino_new->fd = fd;
	fd2ino_new->ino = ino;

  REPLACE_FD2INO(fd2ino_new, fd2ino_old);
	uv_mutex_unlock(&fd2ino_lock);

	return fd2ino_old;
}

static void fd2ino_delete (uv_file fd) {
	fd2ino_t *fd2ino;

	uv_mutex_lock(&fd2ino_lock);
	fd2ino = fd2ino_find(fd);
  if (fd2ino != NULL) {
		HASH_DEL(fd2ino_table, fd2ino);
		uv__free(fd2ino);
	}
	uv_mutex_unlock(&fd2ino_lock);
}

/* While working with the various tables, a thread on the threadpool should not be canceled.
 * This path leads to memory corruption or loss of mutex. */
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


/* High-level interfaces for dealing with the resources used by a request. */

static void mark_resources_slow (uv_fs_t *req) {
	mark_not_cancelable();
	dprintf(2, "mark_resources_slow: TODO Blacklist resources involved in req %p\n", req);
	mark_cancelable();
	return;
}

static int are_resources_slow (uv_fs_t *req) {
	mark_not_cancelable();
	dprintf(2, "are_resources_slow: TODO Consult slow resources tables for req %p\n", req);
	mark_cancelable();
	return 0;
}

/* Various magics for ensuring "good behavior", that runaway threads always work on buffers, etc. */
/* TODO */

#define INIT(subtype)                                                         \
  uv_once(&slow_resources_init_once, slow_resources_init);                    \
  do {                                                                        \
    if (req == NULL)                                                          \
      return -EINVAL;                                                         \
    req->type = UV_FS;                                                        \
    if (cb != NULL)                                                           \
      uv__req_init(loop, req, UV_FS);                                         \
    req->fs_type = UV_FS_ ## subtype;                                         \
    req->result = 0;                                                          \
    req->ptr = NULL;                                                          \
    req->loop = loop;                                                         \
    req->path = NULL;                                                         \
    req->new_path = NULL;                                                     \
    req->cb = cb;                                                             \
  }                                                                           \
  while (0)

#define PATH                                                                  \
  do {                                                                        \
    assert(path != NULL);                                                     \
    if (cb == NULL) {                                                         \
      req->path = path;                                                       \
    } else {                                                                  \
      req->path = uv__strdup(path);                                           \
      if (req->path == NULL) {                                                \
        uv__req_unregister(loop, req);                                        \
        return -ENOMEM;                                                       \
      }                                                                       \
    }                                                                         \
  }                                                                           \
  while (0)

#define PATH2                                                                 \
  do {                                                                        \
    if (cb == NULL) {                                                         \
      req->path = path;                                                       \
      req->new_path = new_path;                                               \
    } else {                                                                  \
      size_t path_len;                                                        \
      size_t new_path_len;                                                    \
      path_len = strlen(path) + 1;                                            \
      new_path_len = strlen(new_path) + 1;                                    \
      req->path = uv__malloc(path_len + new_path_len);                        \
      if (req->path == NULL) {                                                \
        uv__req_unregister(loop, req);                                        \
        return -ENOMEM;                                                       \
      }                                                                       \
      req->new_path = req->path + path_len;                                   \
      memcpy((void*) req->path, path, path_len);                              \
      memcpy((void*) req->new_path, new_path, new_path_len);                  \
    }                                                                         \
  }                                                                           \
  while (0)

#define POST                                                                  \
  do {                                                                        \
    if (cb != NULL) {                                                         \
      uv__work_submit(loop, &req->work_req, uv__fs_work, uv__fs_timed_out, uv__fs_done, uv__fs_killed);        \
      return 0;                                                               \
    }                                                                         \
    else {                                                                    \
      uv__fs_work(&req->work_req);                                            \
      return req->result;                                                     \
    }                                                                         \
  }                                                                           \
  while (0)

/* libuv implementations of FS APIs, for cases where various unices differ. */
#if defined(__APPLE__) && !defined(MAC_OS_X_VERSION_10_8)
#define UV_CONST_DIRENT uv__dirent_t
#else
#define UV_CONST_DIRENT const uv__dirent_t
#endif

/* uv__fs_X: Implementation of API X, dispatched by uv__fs_work. */
static ssize_t uv__fs_fdatasync(uv_fs_t* req);
static ssize_t uv__fs_fsync(uv_fs_t* req);
static ssize_t uv__fs_futime(uv_fs_t* req);
static ssize_t uv__fs_mkdtemp(uv_fs_t* req);
static ssize_t uv__fs_open(uv_fs_t* req);
static ssize_t uv__fs_read(uv_fs_t* req);
static int uv__fs_scandir_filter(UV_CONST_DIRENT* dent);
static int uv__fs_scandir_sort(UV_CONST_DIRENT** a, UV_CONST_DIRENT** b);
static ssize_t uv__fs_scandir(uv_fs_t* req);
static ssize_t uv__fs_pathmax_size(const char* path);
static ssize_t uv__fs_readlink(uv_fs_t* req);
static ssize_t uv__fs_realpath(uv_fs_t* req);
static ssize_t uv__fs_sendfile_emul(uv_fs_t* req);
static ssize_t uv__fs_sendfile(uv_fs_t* req);
static ssize_t uv__fs_utime(uv_fs_t* req);
static ssize_t uv__fs_write(uv_fs_t* req);
static ssize_t uv__fs_copyfile(uv_fs_t* req);
static int uv__fs_stat(const char *path, uv_stat_t *buf);
static int uv__fs_lstat(const char *path, uv_stat_t *buf);
static int uv__fs_fstat(int fd, uv_stat_t *buf);
static int uv__fs_close(uv_fs_t *req);

static ssize_t uv__fs_fdatasync(uv_fs_t* req) {
#if defined(__linux__) || defined(__sun) || defined(__NetBSD__)
  return fdatasync(req->file);
#elif defined(__APPLE__)
  /* Apple's fdatasync and fsync explicitly do NOT flush the drive write cache
   * to the drive platters. This is in contrast to Linux's fdatasync and fsync
   * which do, according to recent man pages. F_FULLFSYNC is Apple's equivalent
   * for flushing buffered data to permanent storage.
   */
  return fcntl(req->file, F_FULLFSYNC);
#else
  return fsync(req->file);
#endif
}


static ssize_t uv__fs_fsync(uv_fs_t* req) {
#if defined(__APPLE__)
  /* See the comment in uv__fs_fdatasync. */
  return fcntl(req->file, F_FULLFSYNC);
#else
  return fsync(req->file);
#endif
}


static ssize_t uv__fs_futime(uv_fs_t* req) {
#if defined(__linux__)
  /* utimesat() has nanosecond resolution but we stick to microseconds
   * for the sake of consistency with other platforms.
   */
  static int no_utimesat;
  struct timespec ts[2];
  struct timeval tv[2];
  char path[sizeof("/proc/self/fd/") + 3 * sizeof(int)];
  int r;

  if (no_utimesat)
    goto skip;

  ts[0].tv_sec  = req->atime;
  ts[0].tv_nsec = (uint64_t)(req->atime * 1000000) % 1000000 * 1000;
  ts[1].tv_sec  = req->mtime;
  ts[1].tv_nsec = (uint64_t)(req->mtime * 1000000) % 1000000 * 1000;

  r = uv__utimesat(req->file, NULL, ts, 0);
  if (r == 0)
    return r;

  if (errno != ENOSYS)
    return r;

  no_utimesat = 1;

skip:

  tv[0].tv_sec  = req->atime;
  tv[0].tv_usec = (uint64_t)(req->atime * 1000000) % 1000000;
  tv[1].tv_sec  = req->mtime;
  tv[1].tv_usec = (uint64_t)(req->mtime * 1000000) % 1000000;
  snprintf(path, sizeof(path), "/proc/self/fd/%d", (int) req->file);

  r = utimes(path, tv);
  if (r == 0)
    return r;

  switch (errno) {
  case ENOENT:
    if (fcntl(req->file, F_GETFL) == -1 && errno == EBADF)
      break;
    /* Fall through. */

  case EACCES:
  case ENOTDIR:
    errno = ENOSYS;
    break;
  }

  return r;

#elif defined(__APPLE__)                                                      \
    || defined(__DragonFly__)                                                 \
    || defined(__FreeBSD__)                                                   \
    || defined(__FreeBSD_kernel__)                                            \
    || defined(__NetBSD__)                                                    \
    || defined(__OpenBSD__)                                                   \
    || defined(__sun)
  struct timeval tv[2];
  tv[0].tv_sec  = req->atime;
  tv[0].tv_usec = (uint64_t)(req->atime * 1000000) % 1000000;
  tv[1].tv_sec  = req->mtime;
  tv[1].tv_usec = (uint64_t)(req->mtime * 1000000) % 1000000;
# if defined(__sun)
  return futimesat(req->file, NULL, tv);
# else
  return futimes(req->file, tv);
# endif
#elif defined(_AIX71)
  struct timespec ts[2];
  ts[0].tv_sec  = req->atime;
  ts[0].tv_nsec = (uint64_t)(req->atime * 1000000) % 1000000 * 1000;
  ts[1].tv_sec  = req->mtime;
  ts[1].tv_nsec = (uint64_t)(req->mtime * 1000000) % 1000000 * 1000;
  return futimens(req->file, ts);
#elif defined(__MVS__)
  attrib_t atr;
  memset(&atr, 0, sizeof(atr));
  atr.att_mtimechg = 1;
  atr.att_atimechg = 1;
  atr.att_mtime = req->mtime;
  atr.att_atime = req->atime;
  return __fchattr(req->file, &atr, sizeof(atr));
#else
  errno = ENOSYS;
  return -1;
#endif
}


static ssize_t uv__fs_mkdtemp(uv_fs_t* req) {
  return mkdtemp((char*) req->path) ? 0 : -1;
}


static ssize_t uv__fs_open(uv_fs_t* req) {
  static int no_cloexec_support;
  int r;
  int rc;
  uv_stat_t statbuf;

  /* If we out after open succeeds, we might leak: fd. */

  /* Try O_CLOEXEC before entering locks */
  if (no_cloexec_support == 0) {
#ifdef O_CLOEXEC
    r = open(req->path, req->flags | O_CLOEXEC, req->mode);
    if (r >= 0)
      return r;
    if (errno != EINVAL)
      return r;
    no_cloexec_support = 1;
#endif  /* O_CLOEXEC */
  }

  if (req->cb != NULL)
    uv_rwlock_rdlock(&req->loop->cloexec_lock);

  r = open(req->path, req->flags, req->mode);

  /* In case of failure `uv__cloexec` will leave error in `errno`,
   * so it is enough to just set `r` to `-1`.
   */
  if (r >= 0 && uv__cloexec(r, 1) != 0) {
    r = uv__close(r);
    if (r != 0)
      abort();
    r = -1;
  }

  if (req->cb != NULL)
    uv_rwlock_rdunlock(&req->loop->cloexec_lock);

  /* Update fd2ino table. */
	rc = uv__fs_fstat(r, &statbuf); /* TODO uv__fs_fstat doesn't know how to time out, it's a worker pool function already. Need sync2async version. */
  if (rc == 0) {
    (void) fd2ino_add(r, statbuf.st_ino);
  }

  /* TODO Need to wipe on close(), but race condition. Perhaps need to add a gennum in the slow_fd table and fd2ino table to deal with this.
   * Make sure the cleanup code isn't busted if close times out. Pondering required. */

  return r;
}


static ssize_t uv__fs_read(uv_fs_t* req) {
#if defined(__linux__)
  static int no_preadv;
#endif
  ssize_t result;

#if defined(_AIX)
  struct stat buf;
  if(fstat(req->file, &buf))
    return -1;
  if(S_ISDIR(buf.st_mode)) {
    errno = EISDIR;
    return -1;
  }
#endif /* defined(_AIX) */
  if (req->off < 0) {
    if (req->nbufs == 1)
      result = read(req->file, req->bufs[0].base, req->bufs[0].len);
    else
      result = readv(req->file, (struct iovec*) req->bufs, req->nbufs);
  } else {
    if (req->nbufs == 1) {
      result = pread(req->file, req->bufs[0].base, req->bufs[0].len, req->off);
      goto done;
    }

#if HAVE_PREADV
    result = preadv(req->file, (struct iovec*) req->bufs, req->nbufs, req->off);
#else
# if defined(__linux__)
    if (no_preadv) retry:
# endif
    {
      off_t nread;
      size_t index;

      nread = 0;
      index = 0;
      result = 1;
      do {
        if (req->bufs[index].len > 0) {
          result = pread(req->file,
                         req->bufs[index].base,
                         req->bufs[index].len,
                         req->off + nread);
          if (result > 0)
            nread += result;
        }
        index++;
      } while (index < req->nbufs && result > 0);
      if (nread > 0)
        result = nread;
    }
# if defined(__linux__)
    else {
      result = uv__preadv(req->file,
                          (struct iovec*)req->bufs,
                          req->nbufs,
                          req->off);
      if (result == -1 && errno == ENOSYS) {
        no_preadv = 1;
        goto retry;
      }
    }
# endif
#endif
  }

done:
  return result;
}


static int uv__fs_scandir_filter(UV_CONST_DIRENT* dent) {
  return strcmp(dent->d_name, ".") != 0 && strcmp(dent->d_name, "..") != 0;
}


static int uv__fs_scandir_sort(UV_CONST_DIRENT** a, UV_CONST_DIRENT** b) {
  return strcmp((*a)->d_name, (*b)->d_name);
}


static ssize_t uv__fs_scandir(uv_fs_t* req) {
  uv__dirent_t **dents;
  int n;

  dents = NULL;
  /* If we time this out, we might leak: fd, memory. */
  n = scandir(req->path, &dents, uv__fs_scandir_filter, uv__fs_scandir_sort);

  /* NOTE: We will use nbufs as an index field */
  req->nbufs = 0;

  if (n == 0) {
    /* OS X still needs to deallocate some memory.
     * Memory was allocated using the system allocator, so use free() here.
     */
    free(dents);
    dents = NULL;
  } else if (n == -1) {
    return n;
  }

  req->ptr = dents;

  return n;
}


static ssize_t uv__fs_pathmax_size(const char* path) {
  ssize_t pathmax;

  pathmax = pathconf(path, _PC_PATH_MAX);

  if (pathmax == -1) {
#if defined(PATH_MAX)
    return PATH_MAX;
#else
#error "PATH_MAX undefined in the current platform"
#endif
  }

  return pathmax;
}

static ssize_t uv__fs_readlink(uv_fs_t* req) {
  ssize_t len;
  char* buf;

  len = uv__fs_pathmax_size(req->path);
  buf = uv__malloc(len + 1);

  if (buf == NULL) {
    errno = ENOMEM;
    return -1;
  }

#if defined(__MVS__)
  len = os390_readlink(req->path, buf, len);
#else
  len = readlink(req->path, buf, len);
#endif


  if (len == -1) {
    uv__free(buf);
    return -1;
  }

  buf[len] = '\0';
  req->ptr = buf;

  return 0;
}

static ssize_t uv__fs_realpath(uv_fs_t* req) {
	int rc;
  ssize_t len;
  char* buf;

  len = uv__fs_pathmax_size(req->path);
  buf = uv__malloc(len + 1);

  if (buf == NULL) {
    errno = ENOMEM;
    return -1;
  }

  /* If we time this out, we might leak: fd, memory. */
  if (realpath(req->path, buf) == NULL) {
    uv__free(buf);
    return -1;
  }

  req->ptr = buf;

  return 0;
}

static ssize_t uv__fs_sendfile_emul(uv_fs_t* req) {
  struct pollfd pfd;
  int use_pread;
  off_t offset;
  ssize_t nsent;
  ssize_t nread;
  ssize_t nwritten;
  size_t buflen;
  size_t len;
  ssize_t n;
  int in_fd;
  int out_fd;
  char buf[8192];

  len = req->bufsml[0].len;
  in_fd = req->flags;
  out_fd = req->file;
  offset = req->off;
  use_pread = 1;

  /* Here are the rules regarding errors:
   *
   * 1. Read errors are reported only if nsent==0, otherwise we return nsent.
   *    The user needs to know that some data has already been sent, to stop
   *    them from sending it twice.
   *
   * 2. Write errors are always reported. Write errors are bad because they
   *    mean data loss: we've read data but now we can't write it out.
   *
   * We try to use pread() and fall back to regular read() if the source fd
   * doesn't support positional reads, for example when it's a pipe fd.
   *
   * If we get EAGAIN when writing to the target fd, we poll() on it until
   * it becomes writable again.
   *
   * FIXME: If we get a write error when use_pread==1, it should be safe to
   *        return the number of sent bytes instead of an error because pread()
   *        is, in theory, idempotent. However, special files in /dev or /proc
   *        may support pread() but not necessarily return the same data on
   *        successive reads.
   *
   * FIXME: There is no way now to signal that we managed to send *some* data
   *        before a write error.
   */
  for (nsent = 0; (size_t) nsent < len; ) {
    buflen = len - nsent;

    if (buflen > sizeof(buf))
      buflen = sizeof(buf);

    do
      if (use_pread)
        nread = pread(in_fd, buf, buflen, offset);
      else
        nread = read(in_fd, buf, buflen);
    while (nread == -1 && errno == EINTR);

    if (nread == 0)
      goto out;

    if (nread == -1) {
      if (use_pread && nsent == 0 && (errno == EIO || errno == ESPIPE)) {
        use_pread = 0;
        continue;
      }

      if (nsent == 0)
        nsent = -1;

      goto out;
    }

    for (nwritten = 0; nwritten < nread; ) {
      do
        n = write(out_fd, buf + nwritten, nread - nwritten);
      while (n == -1 && errno == EINTR);

      if (n != -1) {
        nwritten += n;
        continue;
      }

      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        nsent = -1;
        goto out;
      }

      pfd.fd = out_fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;

      do
        n = poll(&pfd, 1, -1);
      while (n == -1 && errno == EINTR);

      if (n == -1 || (pfd.revents & ~POLLOUT) != 0) {
        errno = EIO;
        nsent = -1;
        goto out;
      }
    }

    offset += nread;
    nsent += nread;
  }

out:
  if (nsent != -1)
    req->off = offset;

  return nsent;
}


static ssize_t uv__fs_sendfile(uv_fs_t* req) {
  int in_fd;
  int out_fd;

  in_fd = req->flags;
  out_fd = req->file;

#if defined(__linux__) || defined(__sun)
  {
    off_t off;
    ssize_t r;

    off = req->off;
    r = sendfile(out_fd, in_fd, &off, req->bufsml[0].len);

    /* sendfile() on SunOS returns EINVAL if the target fd is not a socket but
     * it still writes out data. Fortunately, we can detect it by checking if
     * the offset has been updated.
     */
    if (r != -1 || off > req->off) {
      r = off - req->off;
      req->off = off;
      return r;
    }

    if (errno == EINVAL ||
        errno == EIO ||
        errno == ENOTSOCK ||
        errno == EXDEV) {
      errno = 0;
      return uv__fs_sendfile_emul(req);
    }

    return -1;
  }
#elif defined(__APPLE__)           || \
      defined(__DragonFly__)       || \
      defined(__FreeBSD__)         || \
      defined(__FreeBSD_kernel__)
  {
    off_t len;
    ssize_t r;

    /* sendfile() on FreeBSD and Darwin returns EAGAIN if the target fd is in
     * non-blocking mode and not all data could be written. If a non-zero
     * number of bytes have been sent, we don't consider it an error.
     */

#if defined(__FreeBSD__) || defined(__DragonFly__)
    len = 0;
    r = sendfile(in_fd, out_fd, req->off, req->bufsml[0].len, NULL, &len, 0);
#elif defined(__FreeBSD_kernel__)
    len = 0;
    r = bsd_sendfile(in_fd,
                     out_fd,
                     req->off,
                     req->bufsml[0].len,
                     NULL,
                     &len,
                     0);
#else
    /* The darwin sendfile takes len as an input for the length to send,
     * so make sure to initialize it with the caller's value. */
    len = req->bufsml[0].len;
    r = sendfile(in_fd, out_fd, req->off, &len, NULL, 0);
#endif

     /*
     * The man page for sendfile(2) on DragonFly states that `len` contains
     * a meaningful value ONLY in case of EAGAIN and EINTR.
     * Nothing is said about it's value in case of other errors, so better
     * not depend on the potential wrong assumption that is was not modified
     * by the syscall.
     */
    if (r == 0 || ((errno == EAGAIN || errno == EINTR) && len != 0)) {
      req->off += len;
      return (ssize_t) len;
    }

    if (errno == EINVAL ||
        errno == EIO ||
        errno == ENOTSOCK ||
        errno == EXDEV) {
      errno = 0;
      return uv__fs_sendfile_emul(req);
    }

    return -1;
  }
#else
  /* Squelch compiler warnings. */
  (void) &in_fd;
  (void) &out_fd;

  return uv__fs_sendfile_emul(req);
#endif
}


static ssize_t uv__fs_utime(uv_fs_t* req) {
  struct utimbuf buf;
  buf.actime = req->atime;
  buf.modtime = req->mtime;
  return utime(req->path, &buf); /* TODO use utimes() where available */
}


static ssize_t uv__fs_write(uv_fs_t* req) {
#if defined(__linux__)
  static int no_pwritev;
#endif
  ssize_t r;

  /* Serialize writes on OS X, concurrent write() and pwrite() calls result in
   * data loss. We can't use a per-file descriptor lock, the descriptor may be
   * a dup().
   */
#if defined(__APPLE__)
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

  if (pthread_mutex_lock(&lock))
    abort();
#endif

  if (req->off < 0) {
    if (req->nbufs == 1)
      r = write(req->file, req->bufs[0].base, req->bufs[0].len);
    else
      r = writev(req->file, (struct iovec*) req->bufs, req->nbufs);
  } else {
    if (req->nbufs == 1) {
      r = pwrite(req->file, req->bufs[0].base, req->bufs[0].len, req->off);
      goto done;
    }
#if HAVE_PREADV
    r = pwritev(req->file, (struct iovec*) req->bufs, req->nbufs, req->off);
#else
# if defined(__linux__)
    if (no_pwritev) retry:
# endif
    {
      off_t written;
      size_t index;

      written = 0;
      index = 0;
      r = 0;
      do {
        if (req->bufs[index].len > 0) {
          r = pwrite(req->file,
                     req->bufs[index].base,
                     req->bufs[index].len,
                     req->off + written);
          if (r > 0)
            written += r;
        }
        index++;
      } while (index < req->nbufs && r >= 0);
      if (written > 0)
        r = written;
    }
# if defined(__linux__)
    else {
      r = uv__pwritev(req->file,
                      (struct iovec*) req->bufs,
                      req->nbufs,
                      req->off);
      if (r == -1 && errno == ENOSYS) {
        no_pwritev = 1;
        goto retry;
      }
    }
# endif
#endif
  }

done:
#if defined(__APPLE__)
  if (pthread_mutex_unlock(&lock))
    abort();
#endif

  return r;
}

static ssize_t uv__fs_copyfile(uv_fs_t* req) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  /* On macOS, use the native copyfile(3). */
  copyfile_flags_t flags;

  flags = COPYFILE_ALL;

  if (req->flags & UV_FS_COPYFILE_EXCL)
    flags |= COPYFILE_EXCL;

  return copyfile(req->path, req->new_path, NULL, flags);
#else
  uv_fs_t fs_req;
  uv_file srcfd;
  uv_file dstfd;
  struct stat statsbuf;
  int dst_flags;
  int result;
  int err;
  size_t bytes_to_send;
  int64_t in_offset;

  dstfd = -1;
  err = 0;

  /* Open the source file. */
  srcfd = uv_fs_open(NULL, &fs_req, req->path, O_RDONLY, 0, NULL);
  uv_fs_req_cleanup(&fs_req);

  if (srcfd < 0)
    return srcfd;

  /* Get the source file's mode. */
  if (fstat(srcfd, &statsbuf)) {
    err = -errno;
    goto out;
  }

  dst_flags = O_WRONLY | O_CREAT | O_TRUNC;

  if (req->flags & UV_FS_COPYFILE_EXCL)
    dst_flags |= O_EXCL;

  /* Open the destination file. */
  dstfd = uv_fs_open(NULL,
                     &fs_req,
                     req->new_path,
                     dst_flags,
                     statsbuf.st_mode,
                     NULL);
  uv_fs_req_cleanup(&fs_req);

  if (dstfd < 0) {
    err = dstfd;
    goto out;
  }

  if (fchmod(dstfd, statsbuf.st_mode) == -1) {
    err = -errno;
    goto out;
  }

  bytes_to_send = statsbuf.st_size;
  in_offset = 0;
  while (bytes_to_send != 0) {
    err = uv_fs_sendfile(NULL,
                         &fs_req,
                         dstfd,
                         srcfd,
                         in_offset,
                         bytes_to_send,
                         NULL);
    uv_fs_req_cleanup(&fs_req);
    if (err < 0)
      break;
    bytes_to_send -= fs_req.result;
    in_offset += fs_req.result;
  }

out:
  if (err < 0)
    result = err;
  else
    result = 0;

  /* Close the source file. */
  err = uv__close_nocheckstdio(srcfd);

  /* Don't overwrite any existing errors. */
  if (err != 0 && result == 0)
    result = err;

  /* Close the destination file if it is open. */
  if (dstfd >= 0) {
    err = uv__close_nocheckstdio(dstfd);

    /* Don't overwrite any existing errors. */
    if (err != 0 && result == 0)
      result = err;

    /* Remove the destination file if something went wrong. */
    if (result != 0) {
      uv_fs_unlink(NULL, &fs_req, req->new_path, NULL);
      /* Ignore the unlink return value, as an error already happened. */
      uv_fs_req_cleanup(&fs_req);
    }
  }

  return result;
#endif
}

static void uv__to_stat(struct stat* src, uv_stat_t* dst) {
  dst->st_dev = src->st_dev;
  dst->st_mode = src->st_mode;
  dst->st_nlink = src->st_nlink;
  dst->st_uid = src->st_uid;
  dst->st_gid = src->st_gid;
  dst->st_rdev = src->st_rdev;
  dst->st_ino = src->st_ino;
  dst->st_size = src->st_size;
  dst->st_blksize = src->st_blksize;
  dst->st_blocks = src->st_blocks;

#if defined(__APPLE__)
  dst->st_atim.tv_sec = src->st_atimespec.tv_sec;
  dst->st_atim.tv_nsec = src->st_atimespec.tv_nsec;
  dst->st_mtim.tv_sec = src->st_mtimespec.tv_sec;
  dst->st_mtim.tv_nsec = src->st_mtimespec.tv_nsec;
  dst->st_ctim.tv_sec = src->st_ctimespec.tv_sec;
  dst->st_ctim.tv_nsec = src->st_ctimespec.tv_nsec;
  dst->st_birthtim.tv_sec = src->st_birthtimespec.tv_sec;
  dst->st_birthtim.tv_nsec = src->st_birthtimespec.tv_nsec;
  dst->st_flags = src->st_flags;
  dst->st_gen = src->st_gen;
#elif defined(__ANDROID__)
  dst->st_atim.tv_sec = src->st_atime;
  dst->st_atim.tv_nsec = src->st_atimensec;
  dst->st_mtim.tv_sec = src->st_mtime;
  dst->st_mtim.tv_nsec = src->st_mtimensec;
  dst->st_ctim.tv_sec = src->st_ctime;
  dst->st_ctim.tv_nsec = src->st_ctimensec;
  dst->st_birthtim.tv_sec = src->st_ctime;
  dst->st_birthtim.tv_nsec = src->st_ctimensec;
  dst->st_flags = 0;
  dst->st_gen = 0;
#elif !defined(_AIX) && (       \
    defined(__DragonFly__)   || \
    defined(__FreeBSD__)     || \
    defined(__OpenBSD__)     || \
    defined(__NetBSD__)      || \
    defined(_GNU_SOURCE)     || \
    defined(_BSD_SOURCE)     || \
    defined(_SVID_SOURCE)    || \
    defined(_XOPEN_SOURCE)   || \
    defined(_DEFAULT_SOURCE))
  dst->st_atim.tv_sec = src->st_atim.tv_sec;
  dst->st_atim.tv_nsec = src->st_atim.tv_nsec;
  dst->st_mtim.tv_sec = src->st_mtim.tv_sec;
  dst->st_mtim.tv_nsec = src->st_mtim.tv_nsec;
  dst->st_ctim.tv_sec = src->st_ctim.tv_sec;
  dst->st_ctim.tv_nsec = src->st_ctim.tv_nsec;
# if defined(__FreeBSD__)    || \
     defined(__NetBSD__)
  dst->st_birthtim.tv_sec = src->st_birthtim.tv_sec;
  dst->st_birthtim.tv_nsec = src->st_birthtim.tv_nsec;
  dst->st_flags = src->st_flags;
  dst->st_gen = src->st_gen;
# else
  dst->st_birthtim.tv_sec = src->st_ctim.tv_sec;
  dst->st_birthtim.tv_nsec = src->st_ctim.tv_nsec;
  dst->st_flags = 0;
  dst->st_gen = 0;
# endif
#else
  dst->st_atim.tv_sec = src->st_atime;
  dst->st_atim.tv_nsec = 0;
  dst->st_mtim.tv_sec = src->st_mtime;
  dst->st_mtim.tv_nsec = 0;
  dst->st_ctim.tv_sec = src->st_ctime;
  dst->st_ctim.tv_nsec = 0;
  dst->st_birthtim.tv_sec = src->st_ctime;
  dst->st_birthtim.tv_nsec = 0;
  dst->st_flags = 0;
  dst->st_gen = 0;
#endif
}


static int uv__fs_stat(const char *path, uv_stat_t *buf) {
  struct stat pbuf;
  int ret;

  ret = stat(path, &pbuf);
  if (ret == 0)
    uv__to_stat(&pbuf, buf);

  return ret;
}


static int uv__fs_lstat(const char *path, uv_stat_t *buf) {
  struct stat pbuf;
  int ret;

  ret = lstat(path, &pbuf);
  if (ret == 0)
    uv__to_stat(&pbuf, buf);

  return ret;
}


static int uv__fs_fstat(int fd, uv_stat_t *buf) {
  struct stat pbuf;
  int ret;

  ret = fstat(fd, &pbuf);
  if (ret == 0)
    uv__to_stat(&pbuf, buf);

  return ret;
}

static int uv__fs_close(uv_fs_t *req) {
	int ret;

  /* Since we're about to attempt a close, the fd is going to become invalid.
	 * Per the POSIX spec, we'll either get EBADF, EINTR, or EIO (or time out, due again to EINTR)
	 * If EBADF, lookup in our structures fails and nothing happens.
	 * If EINTR or EIO, the state of filedes afterwards is unspecified,
	 * so nobody should access it anyway.
	 *
	 * See http://pubs.opengroup.org/onlinepubs/9699919799/functions/close.html */
	mark_not_cancelable();
		slow_fd_delete(req->file);
		fd2ino_delete(req->file);
	mark_cancelable();

  /* If we time this out, we might leak: fd. */
	ret = close(req->file);
	return ret;
}


typedef ssize_t (*uv__fs_buf_iter_processor)(uv_fs_t* req);
static ssize_t uv__fs_buf_iter(uv_fs_t* req, uv__fs_buf_iter_processor process) {
  unsigned int iovmax;
  unsigned int nbufs;
  uv_buf_t* bufs;
  ssize_t total;
  ssize_t result;

  iovmax = uv__getiovmax();
  nbufs = req->nbufs;
  bufs = req->bufs;
  total = 0;

  while (nbufs > 0) {
    req->nbufs = nbufs;
    if (req->nbufs > iovmax)
      req->nbufs = iovmax;

    result = process(req);
    if (result <= 0) {
      if (total == 0)
        total = result;
      break;
    }

    if (req->off >= 0)
      req->off += result;

    req->bufs += req->nbufs;
    nbufs -= req->nbufs;
    total += result;
  }

  if (errno == EINTR && total == -1)
    return total;

  if (bufs != req->bufsml)
    uv__free(bufs);

  req->bufs = NULL;
  req->nbufs = 0;

  return total;
}


static void uv__fs_work(struct uv__work* w) {
  int retry_on_eintr;
  uv_fs_t* req;
  ssize_t r;

  req = container_of(w, uv_fs_t, work_req);
  retry_on_eintr = !(req->fs_type == UV_FS_CLOSE);

	if (are_resources_slow(req)) {
		dprintf(2, "uv__fs_work: resource(s) needed by req %p is slow, returning with req->result ETIMEDOUT\n", req);
		req->result = ETIMEDOUT;
		return;
	}

  do {
    errno = 0;

#define X(type, action)                                                       \
  case UV_FS_ ## type:                                                        \
    r = action;                                                               \
    break;

    switch (req->fs_type) {
    X(ACCESS, access(req->path, req->flags));
    X(CHMOD, chmod(req->path, req->mode));
    X(CHOWN, chown(req->path, req->uid, req->gid));
    X(CLOSE, uv__fs_close(req));
    X(COPYFILE, uv__fs_copyfile(req));
    X(FCHMOD, fchmod(req->file, req->mode));
    X(FCHOWN, fchown(req->file, req->uid, req->gid));
    X(FDATASYNC, uv__fs_fdatasync(req));
    X(FSTAT, uv__fs_fstat(req->file, &req->statbuf));
    X(FSYNC, uv__fs_fsync(req));
    X(FTRUNCATE, ftruncate(req->file, req->off));
    X(FUTIME, uv__fs_futime(req));
    X(LSTAT, uv__fs_lstat(req->path, &req->statbuf));
    X(LINK, link(req->path, req->new_path));
    X(MKDIR, mkdir(req->path, req->mode));
    X(MKDTEMP, uv__fs_mkdtemp(req));
    X(OPEN, uv__fs_open(req));
    X(READ, uv__fs_buf_iter(req, uv__fs_read));
    X(SCANDIR, uv__fs_scandir(req));
    X(READLINK, uv__fs_readlink(req));
    X(REALPATH, uv__fs_realpath(req));
    X(RENAME, rename(req->path, req->new_path));
    X(RMDIR, rmdir(req->path));
    X(SENDFILE, uv__fs_sendfile(req));
    X(STAT, uv__fs_stat(req->path, &req->statbuf));
    X(SYMLINK, symlink(req->path, req->new_path));
    X(UNLINK, unlink(req->path));
    X(UTIME, uv__fs_utime(req));
    X(WRITE, uv__fs_buf_iter(req, uv__fs_write));
    default: abort();
    }
#undef X
  } while (r == -1 && errno == EINTR && retry_on_eintr);

  if (r == -1)
    req->result = -errno;
  else
    req->result = r;

  if (r == 0 && (req->fs_type == UV_FS_STAT ||
                 req->fs_type == UV_FS_FSTAT ||
                 req->fs_type == UV_FS_LSTAT)) {
    req->ptr = &req->statbuf;
  }
}


static uint64_t uv__fs_timed_out(struct uv__work* w, void **dat) {
  uv_fs_t* req;

  req = container_of(w, uv_fs_t, work_req);

  dprintf(2, "uv__fs_timed_out: work %p dat %p timed out\n", w, dat);

	/* Propagate to uv__fs_done. */
	req->result = -ETIMEDOUT;

	/* Resource management policy: conservatively mark all resources involved as dangerous. */
	mark_resources_slow(req);
	*dat = NULL;

  /* Tell threadpool to abort the Task. */
	return 0;
}


static void uv__fs_done(struct uv__work* w, int status) {
  uv_fs_t* req;

  req = container_of(w, uv_fs_t, work_req);
  uv__req_unregister(req->loop, req);

  if (status == -ECANCELED) {
    assert(req->result == 0);
		req->result = -ECANCELED;
	}
	else if (status == -ETIMEDOUT) {
		req->result = -ETIMEDOUT;
	}

  req->cb(req);
}


static void uv__fs_killed(void *dat) {
	/* Resource management policy:
	 * The blacklist is permanent, so there's nothing to undo here. */

  /* TODO Various calls use uv__malloc and we must uv__free them here to ensure no leaked memory. */
}


int uv_fs_access(uv_loop_t* loop,
                 uv_fs_t* req,
                 const char* path,
                 int flags,
                 uv_fs_cb cb) {
  INIT(ACCESS);
  PATH;
  req->flags = flags;
  POST;
}


int uv_fs_chmod(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                int mode,
                uv_fs_cb cb) {
  INIT(CHMOD);
  PATH;
  req->mode = mode;
  POST;
}


int uv_fs_chown(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                uv_uid_t uid,
                uv_gid_t gid,
                uv_fs_cb cb) {
  INIT(CHOWN);
  PATH;
  req->uid = uid;
  req->gid = gid;
  POST;
}

int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(CLOSE);
  req->file = file;
  POST;
}


int uv_fs_fchmod(uv_loop_t* loop,
                 uv_fs_t* req,
                 uv_file file,
                 int mode,
                 uv_fs_cb cb) {
  INIT(FCHMOD);
  req->file = file;
  req->mode = mode;
  POST;
}


int uv_fs_fchown(uv_loop_t* loop,
                 uv_fs_t* req,
                 uv_file file,
                 uv_uid_t uid,
                 uv_gid_t gid,
                 uv_fs_cb cb) {
  INIT(FCHOWN);
  req->file = file;
  req->uid = uid;
  req->gid = gid;
  POST;
}


int uv_fs_fdatasync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FDATASYNC);
  req->file = file;
  POST;
}


int uv_fs_fstat(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FSTAT);
  req->file = file;
  POST;
}


int uv_fs_fsync(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb) {
  INIT(FSYNC);
  req->file = file;
  POST;
}


int uv_fs_ftruncate(uv_loop_t* loop,
                    uv_fs_t* req,
                    uv_file file,
                    int64_t off,
                    uv_fs_cb cb) {
  INIT(FTRUNCATE);
  req->file = file;
  req->off = off;
  POST;
}


int uv_fs_futime(uv_loop_t* loop,
                 uv_fs_t* req,
                 uv_file file,
                 double atime,
                 double mtime,
                 uv_fs_cb cb) {
  INIT(FUTIME);
  req->file = file;
  req->atime = atime;
  req->mtime = mtime;
  POST;
}


int uv_fs_lstat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(LSTAT);
  PATH;
  POST;
}


int uv_fs_link(uv_loop_t* loop,
               uv_fs_t* req,
               const char* path,
               const char* new_path,
               uv_fs_cb cb) {
  INIT(LINK);
  PATH2;
  POST;
}


int uv_fs_mkdir(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                int mode,
                uv_fs_cb cb) {
  INIT(MKDIR);
  PATH;
  req->mode = mode;
  POST;
}


int uv_fs_mkdtemp(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* tpl,
                  uv_fs_cb cb) {
  INIT(MKDTEMP);
  req->path = uv__strdup(tpl);
  if (req->path == NULL) {
    if (cb != NULL)
      uv__req_unregister(loop, req);
    return -ENOMEM;
  }
  POST;
}


int uv_fs_open(uv_loop_t* loop,
               uv_fs_t* req,
               const char* path,
               int flags,
               int mode,
               uv_fs_cb cb) {
  INIT(OPEN);
  PATH;
  req->flags = flags;
  req->mode = mode;
  POST;
}


int uv_fs_read(uv_loop_t* loop, uv_fs_t* req,
               uv_file file,
               const uv_buf_t bufs[],
               unsigned int nbufs,
               int64_t off,
               uv_fs_cb cb) {
  INIT(READ);

  if (bufs == NULL || nbufs == 0)
    return -EINVAL;

  req->file = file;

  req->nbufs = nbufs;
  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = uv__malloc(nbufs * sizeof(*bufs));

  if (req->bufs == NULL) {
    if (cb != NULL)
      uv__req_unregister(loop, req);
    return -ENOMEM;
  }

  memcpy(req->bufs, bufs, nbufs * sizeof(*bufs));

  req->off = off;
  POST;
}


int uv_fs_scandir(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* path,
                  int flags,
                  uv_fs_cb cb) {
  INIT(SCANDIR);
  PATH;
  req->flags = flags;
  POST;
}


int uv_fs_readlink(uv_loop_t* loop,
                   uv_fs_t* req,
                   const char* path,
                   uv_fs_cb cb) {
  INIT(READLINK);
  PATH;
  POST;
}


int uv_fs_realpath(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char * path,
                  uv_fs_cb cb) {
  INIT(REALPATH);
  PATH;
  POST;
}


int uv_fs_rename(uv_loop_t* loop,
                 uv_fs_t* req,
                 const char* path,
                 const char* new_path,
                 uv_fs_cb cb) {
  INIT(RENAME);
  PATH2;
  POST;
}


int uv_fs_rmdir(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(RMDIR);
  PATH;
  POST;
}


int uv_fs_sendfile(uv_loop_t* loop,
                   uv_fs_t* req,
                   uv_file out_fd,
                   uv_file in_fd,
                   int64_t off,
                   size_t len,
                   uv_fs_cb cb) {
  INIT(SENDFILE);
  req->flags = in_fd; /* hack */
  req->file = out_fd;
  req->off = off;
  req->bufsml[0].len = len;
  POST;
}


int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(STAT);
  PATH;
  POST;
}


int uv_fs_symlink(uv_loop_t* loop,
                  uv_fs_t* req,
                  const char* path,
                  const char* new_path,
                  int flags,
                  uv_fs_cb cb) {
  INIT(SYMLINK);
  PATH2;
  req->flags = flags;
  POST;
}


int uv_fs_unlink(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
  INIT(UNLINK);
  PATH;
  POST;
}


int uv_fs_utime(uv_loop_t* loop,
                uv_fs_t* req,
                const char* path,
                double atime,
                double mtime,
                uv_fs_cb cb) {
  INIT(UTIME);
  PATH;
  req->atime = atime;
  req->mtime = mtime;
  POST;
}


int uv_fs_write(uv_loop_t* loop,
                uv_fs_t* req,
                uv_file file,
                const uv_buf_t bufs[],
                unsigned int nbufs,
                int64_t off,
                uv_fs_cb cb) {
  INIT(WRITE);

  if (bufs == NULL || nbufs == 0)
    return -EINVAL;

  req->file = file;

  req->nbufs = nbufs;
  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = uv__malloc(nbufs * sizeof(*bufs));

  if (req->bufs == NULL) {
    if (cb != NULL)
      uv__req_unregister(loop, req);
    return -ENOMEM;
  }

  memcpy(req->bufs, bufs, nbufs * sizeof(*bufs));

  req->off = off;
  POST;
}


void uv_fs_req_cleanup(uv_fs_t* req) {
  if (req == NULL)
    return;

  /* Only necessary for asychronous requests, i.e., requests with a callback.
   * Synchronous ones don't copy their arguments and have req->path and
   * req->new_path pointing to user-owned memory.  UV_FS_MKDTEMP is the
   * exception to the rule, it always allocates memory.
   */
  if (req->path != NULL && (req->cb != NULL || req->fs_type == UV_FS_MKDTEMP))
    uv__free((void*) req->path);  /* Memory is shared with req->new_path. */

  req->path = NULL;
  req->new_path = NULL;

  if (req->fs_type == UV_FS_SCANDIR && req->ptr != NULL)
    uv__fs_scandir_cleanup(req);

  if (req->ptr != &req->statbuf)
    uv__free(req->ptr);
  req->ptr = NULL;
}


int uv_fs_copyfile(uv_loop_t* loop,
                   uv_fs_t* req,
                   const char* path,
                   const char* new_path,
                   int flags,
                   uv_fs_cb cb) {
  INIT(COPYFILE);

  if (flags & ~UV_FS_COPYFILE_EXCL)
    return -EINVAL;

  PATH2;
  req->flags = flags;
  POST;
}
