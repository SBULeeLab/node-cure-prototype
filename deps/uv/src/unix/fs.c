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

/* libuv declarations of FS APIs, for cases where various unices differ. */
#if defined(__APPLE__) && !defined(MAC_OS_X_VERSION_10_8)
#define UV_CONST_DIRENT uv__dirent_t
#else
#define UV_CONST_DIRENT const uv__dirent_t
#endif

/* uv__fs_X: synchronous API X, dispatched by uv__fs_work.
 *           These are timeout-unaware. */
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


/**
 * Timeout resource management policy:
 *
 * We manage four structures:
 *  1. Slow Paths (we use a hash of the realpath())
 *  2. Slow FDs
 *  3. Slow Inodes
 *  4. Map of fds to {rph, inode} so we can blacklist based on fd on timeout.
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
  /* rph: realpath hash */
  unsigned rph; /* Key. */
  UT_hash_handle hh;
} slow_rph_t;

typedef struct slow_fd_s {
  uv_file fd; /* Key. */
  UT_hash_handle hh;
} slow_fd_t;

typedef struct slow_ino_s {
  ino_t ino; /* Key. */
  UT_hash_handle hh;
} slow_ino_t;

typedef struct fd2resource_s {
  uv_file fd; /* Key. */

	unsigned rph;
  ino_t ino;

  UT_hash_handle hh;
} fd2resource_t;

static uv_once_t slow_resources_init_once = UV_ONCE_INIT;

static slow_rph_t *slow_rph_table = NULL; 
static uv_mutex_t slow_rph_lock;

static slow_fd_t *slow_fd_table = NULL; 
static uv_mutex_t slow_fd_lock;

static slow_ino_t *slow_ino_table = NULL; 
static uv_mutex_t slow_ino_lock;

static fd2resource_t *fd2resource_table = NULL;  /* Implemented as a hash table keyed by fd, with entries an <fd, ino> pair. */
static uv_mutex_t fd2resource_lock;

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
	if (uv_mutex_init(&fd2resource_lock))
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

/* slow_rph_t* */
#define ADD_RPH(add)                                                          \
    HASH_ADD(hh, slow_rph_table, rph, sizeof(unsigned), add)

/* unsigned* */
#define FIND_RPH(rphP, out)                                                   \
    HASH_FIND(hh, slow_rph_table, rphP, sizeof(unsigned), out)

/* FD */
#define ADD_FD(add)                                                           \
    HASH_ADD(hh, slow_fd_table, fd, sizeof(uv_file), add)

#define FIND_FD(fdP, out)                                                      \
    HASH_FIND(hh, slow_fd_table, fdP, sizeof(uv_file), out)

/* Inode */
#define ADD_INO(add)                                                          \
    HASH_ADD(hh, slow_ino_table, ino, sizeof(ino_t), add)

#define FIND_INO(inoP, out)                                                    \
    HASH_FIND(hh, slow_ino_table, inoP, sizeof(ino_t), out)

/* Macros to access the fd2resourcede map. */
#define ADD_FD2RESOURCE(add)                                                          \
    HASH_ADD(hh, fd2resource_table, fd, sizeof(uv_file), add)

#define FIND_FD2RESOURCE(fdP, out)                                                    \
    HASH_FIND(hh, fd2resource_table, fdP, sizeof(uv_file), out)

#define REPLACE_FD2RESOURCE(add, replaced)                                            \
    HASH_REPLACE(hh, fd2resource_table, fd, sizeof(uv_file), add, replaced)

/* Not-thread-safe APIs for the Slow X hashtables.
 * Caller should wrap calls in 'not cancelable, mutex'.
 * Macros for this purpose precede each set of declarations.
 *
 * This allows the functions to call each other, helpful because uthash requires
 * the *caller* to make sure a key is unique before calling add().
 */
#define BEFORE_SLOW_RPH_TABLE       \
 do {                               \
	 mark_not_cancelable();           \
	 uv_mutex_lock(&slow_rph_lock);   \
 }                                  \
 while (0)

#define AFTER_SLOW_RPH_TABLE        \
 do {                               \
	 uv_mutex_unlock(&slow_rph_lock); \
	 mark_cancelable();               \
 }                                  \
 while (0)

static slow_rph_t * slow_rph_add (unsigned rph);
static slow_rph_t * slow_rph_find (unsigned rph);
static int rph_is_slow (unsigned rph);

#define BEFORE_SLOW_FD_TABLE        \
 do {                               \
	 mark_not_cancelable();           \
	 uv_mutex_lock(&slow_fd_lock);    \
 }                                  \
 while (0)

#define AFTER_SLOW_FD_TABLE         \
 do {                               \
	 uv_mutex_unlock(&slow_fd_lock);  \
	 mark_cancelable();               \
 }                                  \
 while (0)

static slow_fd_t * slow_fd_add (uv_file fd);
static slow_fd_t * slow_fd_find (uv_file fd);
static int fd_is_slow (uv_file fd);
static void slow_fd_delete (uv_file fd);

#define BEFORE_SLOW_INO_TABLE       \
 do {                               \
	 mark_not_cancelable();           \
	 uv_mutex_lock(&slow_ino_lock);   \
 }                                  \
 while (0)

#define AFTER_SLOW_INO_TABLE        \
 do {                               \
	 uv_mutex_unlock(&slow_ino_lock); \
	 mark_cancelable();               \
 }                                  \
 while (0)

static slow_ino_t * slow_ino_add (ino_t ino);
static slow_ino_t * slow_ino_find (ino_t ino);
static int ino_is_slow (ino_t ino);

#define BEFORE_FD2RESOURCE_TABLE            \
 do {                                       \
	 mark_not_cancelable();                   \
	 uv_mutex_lock(&fd2resource_lock);        \
 }                                          \
 while (0)

#define AFTER_FD2RESOURCE_TABLE             \
 do {                                       \
	 uv_mutex_unlock(&fd2resource_lock);      \
	 mark_cancelable();                       \
 }                                          \
 while (0)

static fd2resource_t * fd2resource_add (uv_file fd, ino_t ino, unsigned rph);
static fd2resource_t * fd2resource_find (uv_file fd);
static int fd2resource_known (uv_file fd);
static void fd2resource_delete (uv_file fd);

static slow_rph_t * slow_rph_add (unsigned rph) {
  slow_rph_t *slow_rph = NULL;

	dprintf(2, "slow_rph_add: rph %u\n", rph);

	if (rph_is_slow(rph))
		return NULL;

  slow_rph = (slow_rph_t *) uv__malloc(sizeof(*slow_rph));
	if (slow_rph == NULL)
		abort();
	slow_rph->rph = rph;

  ADD_RPH(slow_rph);

	return slow_rph;
}

static slow_rph_t * slow_rph_find (unsigned rph) {
  slow_rph_t *result;

	dprintf(2, "slow_rph_find: rph %u\n", rph);

  FIND_RPH(&rph, result);

	if (result == NULL)
		dprintf(2, "slow_rph_find: rph %u was not slow yet\n", rph);
  else
		dprintf(2, "slow_rph_find: rph %u is already slow\n", rph);

	return result;
}

static int rph_is_slow (unsigned rph) {
	return (slow_rph_find(rph) != NULL);
}

static slow_fd_t * slow_fd_add (uv_file fd) {
  slow_fd_t *slow_fd = NULL;

  dprintf(2, "slow_fd_add: fd %d\n", fd);

	if (fd_is_slow(fd))
		return NULL;

  slow_fd = (slow_fd_t *) uv__malloc(sizeof(*slow_fd));
	if (slow_fd == NULL)
		abort();
	slow_fd->fd = fd;

  ADD_FD(slow_fd);
	return slow_fd;
}

static slow_fd_t * slow_fd_find (uv_file fd) {
  slow_fd_t *result;

  dprintf(2, "slow_fd_find: %d\n", fd);

  FIND_FD(&fd, result);

	if (result == NULL)
		dprintf(2, "slow_fd_find: fd %d was not slow yet\n", fd);
  else
		dprintf(2, "slow_fd_find: fd %d was already slow\n", fd);

	return result;
}

static int fd_is_slow (uv_file fd) {
	return (slow_fd_find(fd) != NULL);
}

static void slow_fd_delete (uv_file fd) {
	slow_fd_t *slow_fd = NULL;

  dprintf(2, "slow_fd_delete: fd %d\n", fd);

	slow_fd = slow_fd_find(fd);
  if (slow_fd != NULL) {
		HASH_DEL(slow_fd_table, slow_fd);
		uv__free(slow_fd);
	}

	return;
}

static slow_ino_t * slow_ino_add (ino_t ino) {
  slow_ino_t *slow_ino = NULL;

  dprintf(2, "slow_ino_add: ino %lu\n", ino);

	if (ino_is_slow(ino))
		return NULL;

  slow_ino = (slow_ino_t *) uv__malloc(sizeof(*slow_ino));
	if (slow_ino == NULL)
		abort();
	slow_ino->ino = ino;

  ADD_INO(slow_ino);

	return slow_ino;
}

static slow_ino_t * slow_ino_find (ino_t ino) {
  slow_ino_t *result;

  dprintf(2, "slow_ino_find: ino %lu\n", ino);

  FIND_INO(&ino, result);

	if (result == NULL)
		dprintf(2, "slow_ino_find: ino %lu was not slow yet\n", ino);
  else
		dprintf(2, "slow_ino_find: ino %lu was already slow\n", ino);

	return result;
}

static int ino_is_slow (ino_t ino) {
	return (slow_ino_find(ino) != NULL);
}


static fd2resource_t * fd2resource_add (uv_file fd, ino_t ino, unsigned rph) {
  fd2resource_t *fd2resource = NULL;

  dprintf(2, "fd2resource_add: fd %d -> <ino %lu, rph %u>\n", fd, ino, rph);

	if (fd < 0)
		abort();

	if (fd2resource_known(fd))
		abort(); /* One fd table per process, so should open() and get an fd we already know. */

  fd2resource = (fd2resource_t *) uv__malloc(sizeof(*fd2resource));
	if (fd2resource == NULL)
		abort();
	fd2resource->fd = fd;
	fd2resource->ino = ino;
	fd2resource->rph = rph;

  ADD_FD2RESOURCE(fd2resource);

	return fd2resource;
}

static fd2resource_t * fd2resource_find (uv_file fd) {
  fd2resource_t *result = NULL;

  dprintf(2, "fd2resource_find: %d\n", fd);

  FIND_FD2RESOURCE(&fd, result);

	if (result == NULL)
		dprintf(2, "fd2resource_find: No mapping for fd %d\n", fd);
  else
		dprintf(2, "fd2resource_find: Found a mapping for fd %d\n", fd);

	return result;
}

static int fd2resource_known (uv_file fd) {
	return (fd2resource_find(fd) != NULL);
}

static void fd2resource_delete (uv_file fd) {
	fd2resource_t *fd2resource = NULL;

  dprintf(2, "fd2resource_delete: fd %d\n", fd);

	fd2resource = fd2resource_find(fd);
  if (fd2resource != NULL) {
		HASH_DEL(fd2resource_table, fd2resource);
		uv__free(fd2resource);
	}

	return;
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

/* Interfaces for dealing with the resources used by a request. */
static unsigned hash_buf (void *buf, size_t len) {
	unsigned key_hash;
	HASH_VALUE(buf, len, key_hash);
	return key_hash;
}

static unsigned hash_str (char *str) {
	return hash_buf(str, strlen(str));
}

/* These APIs interact with a single file using an fd. 
 *   uv_fs_sendfile can timeout on each end of the path
 *   and blacklisting both ends seems too conservative. */
static int req_has_fd (uv_fs_t *req) {
	return req->fs_type == UV_FS_CLOSE      ||
					req->fs_type == UV_FS_FCHMOD    ||
					req->fs_type == UV_FS_FCHOWN    ||
					req->fs_type == UV_FS_FDATASYNC ||
					req->fs_type == UV_FS_FSTAT     ||
					req->fs_type == UV_FS_FSYNC     ||
					req->fs_type == UV_FS_FTRUNCATE ||
					req->fs_type == UV_FS_FUTIME    ||
					req->fs_type == UV_FS_READ      ||
					req->fs_type == UV_FS_WRITE;
}

/* These APIs interact with a single file using a path.
 *   link, symlink, rename, copyfile, etc. can all timeout on each end of the path
 *   and blacklisting both ends seems too conservative. */
static int req_has_path (uv_fs_t *req) {
	return req->fs_type == UV_FS_ACCESS || /* Ony use if req->flags don't include AT_SYMLINK_NOFOLLOW. */
					req->fs_type == UV_FS_CHMOD ||
					req->fs_type == UV_FS_CHOWN ||
					req->fs_type == UV_FS_OPEN ||
					req->fs_type == UV_FS_SCANDIR ||
					req->fs_type == UV_FS_STAT ||
					req->fs_type == UV_FS_UTIME;
}

/* Call off the synchronous path, e.g. at the top of uv__fs_work.
 * Populates the resources fields of req->timeout_buf for reference:
 *   - on successful open, fd2resource_s
 *   - on timeout, to mark resources as slow
 */
static void store_resources_in_timeout_buf (uv_fs_t *req) {
	int rc;
  uv_stat_t statbuf;
	ino_t ino = 0;
	unsigned rph = 0;
	uv_file file = -1;
	int resources_known = 0;

  /* Should only happen once... */
  if (req->timeout_buf->resources_set)
		abort();

	/* TODO 2 < fd: Caller might close stdout and stderr, but ignore for now. */
	if (req_has_fd(req) && 2 < req->file) {
		fd2resource_t *fd2resource;
		dprintf(2, "store_resources_in_timeout_buf: req %p has an fd so we can get info from fd2resources\n", req);

		BEFORE_FD2RESOURCE_TABLE;
			fd2resource = fd2resource_find(req->file);
			if (fd2resource == NULL)
				abort(); /* TODO Caller can do this and we should propagate an error message. But for now we can make sure fd2resource is stable. */
		AFTER_FD2RESOURCE_TABLE;

		ino = fd2resource->ino;
		rph = fd2resource->rph;
		file = req->file;
		resources_known = 1;
	}
	else if (req_has_path(req)) {
		/* If request uses a path, obtain the rph and inode number.
		 * Start with inode number because it doesn't risk an fd leak, and might even time out early for us! */
		dprintf(2, "store_resources_in_timeout_buf: req %p has path %s\n", req, req->timeout_buf->path);
		rc = uv__fs_stat(req->timeout_buf->path, &statbuf);
		if (rc < 0)
			return;
		ino = statbuf.st_ino;

		/* First let's get the rph and inode number.
		 * These can be done without risking an fd leak, and we expect stat cost to be a good predictor of open time.
		 * Counterexample: open a fifo with O_RDONLY can hang. */
		rc = uv__fs_realpath(req);
		if (rc != 0)
			return;
		rph = hash_str(req->ptr);
		resources_known = 1;
	}

	/* Save these resource values in timeout_buf for reference on timeout. */
	if (resources_known) {
		mark_not_cancelable();
			req->timeout_buf->rph = rph;
			req->timeout_buf->ino = ino;
			req->timeout_buf->file = file;
			req->timeout_buf->resources_set = 1;
		mark_cancelable();
	}

	return;
}

/* POLICY DECISION: The resources of this req are slow. */
static void mark_resources_slow (uv_fs_t *req) {
	mark_not_cancelable();

	if (req->timeout_buf->resources_set) {
		dprintf(2, "mark_resources_slow: req %p: rph %u ino %lu fd %d\n", req, req->timeout_buf->rph, req->timeout_buf->ino, req->timeout_buf->file);
		mark_not_cancelable();

		uv_mutex_lock(&slow_rph_lock);
		slow_rph_add(req->timeout_buf->rph);
		uv_mutex_unlock(&slow_rph_lock);

		uv_mutex_lock(&slow_ino_lock);
		slow_ino_add(req->timeout_buf->ino);
		uv_mutex_unlock(&slow_ino_lock);

		if (0 <= req->timeout_buf->file) { /* Might not be known. */
			uv_mutex_lock(&slow_fd_lock);
			slow_fd_add(req->timeout_buf->file);
			uv_mutex_unlock(&slow_fd_lock);
		}

		mark_cancelable();
	}
	else 
		dprintf(2, "mark_resources_slow: req %p: Sorry, no known resources\n", req);

	mark_cancelable();
	return;
}

/* POLICY DECISION: Are the resources of this req slow? */
static int are_resources_slow (uv_fs_t *req) {
	int are_slow = 0;

	mark_not_cancelable();

	if (req->timeout_buf->resources_set) {
		uv_mutex_lock(&slow_rph_lock);
		if (rph_is_slow(req->timeout_buf->rph)) {
			dprintf(2, "are_resources_slow: req %p rph %u is slow\n", req, req->timeout_buf->rph);
			are_slow = 1;
		}
		uv_mutex_unlock(&slow_rph_lock);

		uv_mutex_lock(&slow_ino_lock);
		if (ino_is_slow(req->timeout_buf->ino)) {
			dprintf(2, "are_resources_slow: req %p ino %lu is slow\n", req, req->timeout_buf->ino);
			are_slow = 1;
		}
		uv_mutex_unlock(&slow_ino_lock);

		uv_mutex_lock(&slow_fd_lock);
		if (0 <= req->timeout_buf->file && fd_is_slow(req->timeout_buf->file)) {
			dprintf(2, "are_resources_slow: req %p fd %d is slow\n", req, req->timeout_buf->file);
			are_slow = 1;
		}
		uv_mutex_unlock(&slow_fd_lock);
	}
	else
		dprintf(2, "are_resources_slow: req %p resources unknown\n", req);

	mark_cancelable();

	dprintf(2, "are_resources_slow: req %p are_slow %d\n", req, are_slow);
	return are_slow;
}

/* uv_fs_t* has a timeout_buf for "good behavior".
 * A runaway thread must always be working on memory that we allocated,
 * and on success we'll copy the buffer over to the user's buffers. */

/* Initialize with refcount 0. */
static uv__fs_buf_t * uv__fs_buf_create (void) {
	uv__fs_buf_t *buf;
	
	buf = (uv__fs_buf_t *) uv__malloc(sizeof(*buf));
	if (buf == NULL)
		return NULL;
	
	buf->resources_set = 0;
	buf->rph = 0;
	buf->ino = 0;
	buf->file = -1;
	
	buf->io_bufs = NULL;
	buf->io_orig_bases = NULL;
	buf->io_nbufs = 0;

	buf->path = NULL;
	buf->new_path = NULL;
	buf->tmp_path = NULL;

	buf->statbuf = uv__malloc(sizeof(*buf->statbuf));
	if (buf->statbuf == NULL) {
		uv__free(buf);
		return NULL;
	}

	if (uv_mutex_init(&buf->mutex) != 0)
		abort();
	
	if (uv_sem_init(&buf->done, 0) != 0)
		abort();

	buf->refcount = 0; 

	dprintf(2, "uv__fs_buf_create: Return'ing buf %p\n", buf);
	return buf;
}

/* Add a reference-r. Caller should not hold lock. */ 
static void uv__fs_buf_ref (uv__fs_buf_t *buf) {
	if (buf != NULL) {
		dprintf(2, "uv__fs_buf_ref: Ref'ing buf %p\n", buf);
		uv_mutex_lock(&buf->mutex);
		buf->refcount++;
		uv_mutex_unlock(&buf->mutex);
	}

	return;
}

static void uv__fs_buf_destroy (uv__fs_buf_t *buf) {
	unsigned int i;

	if (buf == NULL)
		return;

	dprintf(2, "uv__fs_buf_unref: Destroy'ing buf %p\n", buf);

	/* Clean up any timeout-safe memory. */
	/* io_bufs: free each uv_buf_t's base, then io_bufs itself. */
	if (buf->io_bufs != NULL) {
		for (i = 0; i < buf->io_nbufs; i++) {
			uv__free(buf->io_bufs[i].base);
			buf->io_bufs[i].base = NULL;
		}
		uv__free(buf->io_bufs);
		buf->io_bufs = NULL;
	}

	if (buf->io_orig_bases != NULL) {
		uv__free(buf->io_orig_bases);
		buf->io_orig_bases = NULL;
	}

	/* Memory is shared with buf->new_path. */
  /* TODO Possible re-optimization point.
	 *      Can share user's path memory on sync requests, and don't deallocate here. */
	if (buf->path != NULL) {
		uv__free(buf->path);
		buf->path = NULL;
	}

	if (buf->tmp_path != NULL) {
		uv__free(buf->tmp_path);
		buf->tmp_path = NULL;
	}

	if (buf->statbuf != NULL) {
		uv__free(buf->statbuf);
		buf->statbuf = NULL;
	}

	uv_mutex_destroy(&buf->mutex);
	uv_sem_destroy(&buf->done);

	uv__free(buf);
	buf = NULL;
}

/* The last dereference-r destroys it. Caller should not hold lock. */
static void uv__fs_buf_unref (uv__fs_buf_t *buf) {
	int destroy_timeout_buf;

	if (buf == NULL)
		return;
	
	dprintf(2, "uv__fs_buf_unref: Unref'ing buf %p\n", buf);

	/* We hold one of the references to timeout_buf, see if we hold the last one. */ 
	destroy_timeout_buf = 0;
	uv_mutex_lock(&buf->mutex);
	buf->refcount--;
	if (buf->refcount == 0)
		destroy_timeout_buf = 1;
	uv_mutex_unlock(&buf->mutex);

	if (destroy_timeout_buf)
		uv__fs_buf_destroy(buf);

	return;
}

/* Return 0 on success. */
int uv__fs_buf_copy_io_bufs(uv__fs_buf_t *fs_buf, const uv_buf_t bufs[], unsigned int nbufs) {
	unsigned int i = 0;
	unsigned int nallocated = 0;

	if (fs_buf == NULL)
		abort();
	
	if (nbufs == 0)
		return 0;
	
	if (fs_buf->io_bufs != NULL)
		abort();

	fs_buf->io_bufs = (uv_buf_t *) uv__malloc(nbufs*sizeof(*fs_buf->io_bufs));
	if (fs_buf->io_bufs == NULL)
		return -ENOMEM;
	
	fs_buf->io_orig_bases = (char **) uv__malloc(nbufs*sizeof(char *));
	if (fs_buf->io_orig_bases == NULL)
		goto ERROR_ENOMEM;
	
	for (i = 0; i < nbufs; i++) {
		fs_buf->io_bufs[i].base = (char *) uv__malloc(bufs[i].len);
		if (fs_buf->io_bufs[i].base == NULL)
			break;
		fs_buf->io_bufs[i].len = bufs[i].len;
		fs_buf->io_orig_bases[i] = bufs[i].base; /* Save orig pointer for copy back on success. */
		/* Required for write, allows caller to use read canaries on read. */
		memcpy(fs_buf->io_bufs[i].base, bufs[i].base, bufs[i].len);

		nallocated++;
	}

	if (nallocated < nbufs)
		goto ERROR_ENOMEM;
	
	fs_buf->io_nbufs = nbufs;
	return 0;
	
ERROR_ENOMEM:
  /* Allocation issue on one of the io_bufs. Release all memory and return in error. */
	if (fs_buf->io_orig_bases) {
		uv__free(fs_buf->io_orig_bases);
		fs_buf->io_orig_bases = NULL;
	}

	if (fs_buf->io_bufs) {
		for (i = 0; i < nallocated; i++) {
			uv__free(fs_buf->io_bufs[i].base);
			fs_buf->io_bufs[i].base = NULL;
		}

		uv__free(fs_buf->io_bufs);
		fs_buf->io_bufs = NULL;
	}

	return -ENOMEM;
}

/* TODO */
static int is_stat_type (int type) {
	return type == UV_FS_STAT ||
	       type == UV_FS_LSTAT ||
				 type == UV_FS_FSTAT;
}


static int type_needs_path (int type) {
	/* All the calls that take a path, from uv.h */
	return type == UV_FS_OPEN ||
					type == UV_FS_UNLINK ||
					type == UV_FS_COPYFILE ||
					type == UV_FS_MKDIR ||
					type == UV_FS_MKDTEMP ||
					type == UV_FS_RMDIR ||
					type == UV_FS_SCANDIR ||
					type == UV_FS_STAT ||
					type == UV_FS_RENAME ||
					type == UV_FS_ACCESS ||
					type == UV_FS_CHMOD ||
					type == UV_FS_UTIME ||
					type == UV_FS_LSTAT ||
					type == UV_FS_LINK ||
					type == UV_FS_SYMLINK ||
					type == UV_FS_READLINK ||
					type == UV_FS_REALPATH ||
					type == UV_FS_CHOWN;
}

static void sync_timeout_buf (uv_fs_t *req, int success) {
	/* Stat. */
	if (is_stat_type(req->fs_type) && success) {
		dprintf(2, "sync_timeout_buf: copying statbuf\n");
		memcpy(&req->statbuf, req->timeout_buf->statbuf, sizeof(req->statbuf));
    req->ptr = &req->statbuf; /* The uv__fs_xstat calls don't get a uv_fs_t*, so they can't update req themselves. */
	}
	else if (type_needs_path(req->fs_type)) {
		dprintf(2, "sync_timeout_buf: swinging path\n");
		req->path = req->timeout_buf->path;
	}
	else if (req->fs_type == UV_FS_READ && success) {
		ssize_t nbytes_left, this_buf_len, bytes_to_copy;
		unsigned int i = 0;
		
		nbytes_left = req->result;

		dprintf(2, "sync_timeout_buf: copying %ld bytes into %u buffers\n", nbytes_left, req->timeout_buf->io_nbufs);

		while (0 < nbytes_left) {
			if (req->timeout_buf->io_nbufs <= i) abort();

			this_buf_len = req->timeout_buf->io_bufs[i].len;
			bytes_to_copy = this_buf_len < nbytes_left ? this_buf_len : nbytes_left;

			/* Fill buffer i. */
			memcpy(req->timeout_buf->io_orig_bases[i], req->timeout_buf->io_bufs[i].base, bytes_to_copy);

			nbytes_left -= bytes_to_copy;
			i++; /* Move on to next buffer. */
		}
	}

	/* TODO Anything else? */

}

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
    req->cb = cb;                                                             \
		req->timeout_buf = uv__fs_buf_create();                                   \
		if (req->timeout_buf == NULL) {                                           \
			return -ENOMEM;                                                         \
		}                                                                         \
		uv__fs_buf_ref(req->timeout_buf);                                         \
  }                                                                           \
  while (0)

/* JD: I removed the synchronous case in PATH and PATH2 for ease of uv__fs_buf_deref cleanup.
 *     This is a possible re-optimization point -- check whether request was sync or async
 *     and wipe the paths before uv__fs_buf_deref if it was sync. */
#define PATH                                                                  \
  do {                                                                        \
    assert(path != NULL);                                                     \
    req->timeout_buf->path = uv__strdup(path);                                \
    if (req->timeout_buf->path == NULL) {                                     \
			uv__fs_buf_unref(req->timeout_buf);                                     \
      if (req->cb) uv__req_unregister(loop, req);                             \
      return -ENOMEM;                                                         \
    }                                                                         \
  }                                                                           \
  while (0)

#define PATH2                                                                 \
  do {                                                                        \
    size_t path_len;                                                          \
    size_t new_path_len;                                                      \
    path_len = strlen(path) + 1;                                              \
    new_path_len = strlen(new_path) + 1;                                      \
    req->timeout_buf->path = uv__malloc(path_len + new_path_len);             \
    if (req->timeout_buf->path == NULL) {                                     \
		  uv__fs_buf_unref(req->timeout_buf);                                     \
      if (req->cb) uv__req_unregister(loop, req);                             \
      return -ENOMEM;                                                         \
    }                                                                         \
    req->timeout_buf->new_path = req->timeout_buf->path + path_len;           \
    memcpy((void*) req->timeout_buf->path, path, path_len);                   \
    memcpy((void*) req->timeout_buf->new_path, new_path, new_path_len);       \
  }                                                                           \
  while (0)

/* POST: With cb, take normal path. Without, submit prio work and wait for it to complete. */
#define POST                                                                  \
  do {                                                                        \
    if (cb == NULL) {                                                         \
      uv__work_submit_prio(loop, &req->work_req, uv__fs_work, uv__fs_timed_out, uv__fs_done_sync, uv__fs_killed);        \
			uv_sem_wait(&req->timeout_buf->done);                                   \
      return req->result;                                                     \
    }                                                                         \
    else {                                                                    \
      uv__work_submit(loop, &req->work_req, uv__fs_work, uv__fs_timed_out, uv__fs_done, uv__fs_killed);        \
      return 0;                                                               \
    }                                                                         \
  }                                                                           \
  while (0)


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
  return mkdtemp((char*) req->timeout_buf->path) ? 0 : -1;
}

static ssize_t uv__fs_open(uv_fs_t* req) {
  static int no_cloexec_support;
  int r;

	/* If either resource is slow, return in error. */

  /* If we time out during or after an otherwise-successful open, we might leak: fd. */

  /* Try O_CLOEXEC before entering locks */
  if (no_cloexec_support == 0) {
#ifdef O_CLOEXEC
    r = open(req->timeout_buf->path, req->flags | O_CLOEXEC, req->mode);
    if (r >= 0) {

			/* If we created a new file, update the path and the inode.
			 * We do this under not_cancelable because if open succeeded then this info is cached. */
			mark_not_cancelable();
				if (req->timeout_buf->ino == 0) {
					dprintf(2, "uv__fs_open: created new file %s, looking up the resources\n", req->timeout_buf->path);
					store_resources_in_timeout_buf(req); /* rph, ino */
					if (!req->timeout_buf->resources_set)
						abort();
				}
				/* Now we have enough to update the fd2resource table. */
				req->timeout_buf->file = r;
				dprintf(2, "uv__fs_open: %s, new fd2resource: %d -> <%lu, %u>\n", req->timeout_buf->path, req->timeout_buf->file, req->timeout_buf->ino, req->timeout_buf->rph);

				uv_mutex_lock(&fd2resource_lock);
				fd2resource_add(req->timeout_buf->file, req->timeout_buf->ino, req->timeout_buf->rph);
				uv_mutex_unlock(&fd2resource_lock);
			mark_cancelable();

      return r;
		}
    if (errno != EINVAL)
      return r;
    no_cloexec_support = 1;
#endif  /* O_CLOEXEC */
  }

	abort(); /* Should never get here on Linux. */

  if (req->cb != NULL)
    uv_rwlock_rdlock(&req->loop->cloexec_lock);

  r = open(req->timeout_buf->path, req->flags, req->mode);

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

  /* Caller is supposed to have set req->bufs to point to some buf allocated in req->timeout_buf->io_bufs. */
	if (req->timeout_buf->io_bufs <= req->bufs &&
		  req->bufs < req->timeout_buf->io_bufs + req->timeout_buf->io_nbufs) {
	}
	else
		abort();

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
  /* If we time this out, scandir itself might leak: fd, memory. */
  n = scandir(req->timeout_buf->path, &dents, uv__fs_scandir_filter, uv__fs_scandir_sort);

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

  len = uv__fs_pathmax_size(req->timeout_buf->path);

  /* Allocate in timeout_buf so if we are interrupted during realpath we can free in uv_fs_req_cleanup. */
  req->timeout_buf->tmp_path = uv__malloc(len + 1);
  if (req->timeout_buf->tmp_path == NULL) {
    errno = ENOMEM;
    return -1;
  }

#if defined(__MVS__)
  len = os390_readlink(req->timeout_buf->path, req->timeout_buf->tmp_path, len);
#else
  len = readlink(req->timeout_buf->path, req->timeout_buf->tmp_path, len);
#endif

	if (0 <= len) {
		req->timeout_buf->tmp_path[len] = '\0';
		req->ptr = req->timeout_buf->tmp_path; /* Caller must copy this out before he calls uv_fs_req_cleanup. */
		return 0;
	}
	else
		return -1;
}

/* On success, req->ptr points to the realpath. */
static ssize_t uv__fs_realpath(uv_fs_t* req) {
  ssize_t len;

	if (req->timeout_buf->resources_set) {
		/* Surprise, we already know this because we looked it up in uv__fs_work via store_resources_in_timeout_buf. */
		dprintf(2, "uv__fs_realpath: we already know that %s -> %s\n", req->timeout_buf->path, req->ptr);
		return 0;
	}
	else {
		dprintf(2, "uv__fs_realpath: path %s\n", req->timeout_buf->path);

		len = uv__fs_pathmax_size(req->timeout_buf->path);

		/* Allocate in timeout_buf so if we are interrupted during realpath we can free in uv_fs_req_cleanup. */
		req->timeout_buf->tmp_path = uv__malloc(len + 1);
		if (req->timeout_buf->tmp_path == NULL) {
			errno = ENOMEM;
			return -1;
		}

		/* If we time this out, realpath itself might leak: fd, memory. */
		if (realpath(req->timeout_buf->path, req->timeout_buf->tmp_path) == NULL) {
			dprintf(2, "uv__fs_realpath: realpath failed: %d %s\n", errno, uv_strerror(errno));
			return -1;
		}

		req->ptr = req->timeout_buf->tmp_path;
		dprintf(2, "uv__fs_realpath: realpath %s\n", req->ptr);
		return 0;
	}
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

  len = req->timeout_buf->io_bufs[0].len;
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
    r = sendfile(out_fd, in_fd, &off, req->timeout_buf->io_bufs[0].len);

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
    r = sendfile(in_fd, out_fd, req->off, req->timeout_buf->io_bufs[0].len, NULL, &len, 0);
#elif defined(__FreeBSD_kernel__)
    len = 0;
    r = bsd_sendfile(in_fd,
                     out_fd,
                     req->off,
                     req->timeout_buf->io_bufs[0].len,
                     NULL,
                     &len,
                     0);
#else
    /* The darwin sendfile takes len as an input for the length to send,
     * so make sure to initialize it with the caller's value. */
    len = req->timeout_buf->io_bufs[0].len;
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
  return utime(req->timeout_buf->path, &buf); /* TODO use utimes() where available */
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

  /* Caller is supposed to have set req->bufs to point to some buf allocated in req->bufs. */
	if (req->bufs <= req->bufs &&
		  req->bufs < req->bufs + req->nbufs) {
	}
	else
		abort();

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

/* TODO fd leak. We could save srcfd and dstfd in req->timeout_buf so that if the second uv_fs_open times out, we can still clean up srcfd. */
static ssize_t uv__fs_copyfile(uv_fs_t* req) {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  /* On macOS, use the native copyfile(3). */
  copyfile_flags_t flags;

  flags = COPYFILE_ALL;

  if (req->flags & UV_FS_COPYFILE_EXCL)
    flags |= COPYFILE_EXCL;

  return copyfile(req->timeout_buf->path, req->timeout_buf->new_path, NULL, flags);
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

  /* TODO Not supported due to fd2resource leak below in uv__close_nocheckstdio. */
	abort();

  /* Open the source file. */
  srcfd = uv_fs_open(NULL, &fs_req, req->timeout_buf->path, O_RDONLY, 0, NULL);
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
                     req->timeout_buf->new_path,
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
	/* TODO We uv_fs_open'd so fd2resource has a mapping but we never delete this mapping. */
  err = uv__close_nocheckstdio(srcfd);

  /* Don't overwrite any existing errors. */
  if (err != 0 && result == 0)
    result = err;

  /* Close the destination file if it is open. */
  if (dstfd >= 0) {
		/* TODO We uv_fs_open'd so fd2resource has a mapping but we never delete this mapping. */
    err = uv__close_nocheckstdio(dstfd);

    /* Don't overwrite any existing errors. */
    if (err != 0 && result == 0)
      result = err;

    /* Remove the destination file if something went wrong. */
    if (result != 0) {
      uv_fs_unlink(NULL, &fs_req, req->timeout_buf->new_path, NULL);
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

	dprintf(2, "uv__fs_close: req %p fd %d\n", req, req->file);

  /* Since we're about to attempt a close, the fd is going to become invalid.
	 * Per the POSIX spec, we'll either get EBADF, EINTR, or EIO (or time out, due again to EINTR)
	 * If EBADF, lookup in our structures fails and nothing happens.
	 * If EINTR or EIO, the state of filedes afterwards is unspecified,
	 * so nobody should access it anyway.
	 *
	 * See http://pubs.opengroup.org/onlinepubs/9699919799/functions/close.html */
	mark_not_cancelable();
		uv_mutex_lock(&slow_fd_lock);
		slow_fd_delete(req->timeout_buf->file);
		uv_mutex_unlock(&slow_fd_lock);

		uv_mutex_lock(&fd2resource_lock);
		fd2resource_delete(req->timeout_buf->file);
		uv_mutex_unlock(&fd2resource_lock);

		req->timeout_buf->file = -1; /* fd is now dead. */
	mark_cancelable();

  /* If we time this out, close might leak: fd. */
	ret = close(req->file);
	dprintf(2, "uv__fs_close: req %p fd %d ret %d\n", req, req->file, ret);
	return ret;
}

typedef ssize_t (*uv__fs_buf_iter_processor)(uv_fs_t* req);
static ssize_t uv__fs_buf_iter(uv_fs_t* req, uv__fs_buf_iter_processor process) {
  unsigned int iovmax;
  unsigned int nbufs;
  ssize_t total;
  ssize_t result;

  iovmax = uv__getiovmax();
  nbufs = req->timeout_buf->io_nbufs;
	req->bufs = req->timeout_buf->io_bufs;
	req->nbufs = nbufs;
  total = 0;

	req->io_bufs_ix = 0;

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
		req->io_bufs_ix += req->nbufs;
    nbufs -= req->nbufs;
    total += result;
  }

  if (errno == EINTR && total == -1)
    return total;

  return total;
}


static void uv__fs_work(struct uv__work* w) {
  int retry_on_eintr;
  uv_fs_t* req;
  ssize_t r;

  req = container_of(w, uv_fs_t, work_req);
  retry_on_eintr = !(req->fs_type == UV_FS_CLOSE);

	dprintf(2, "uv__fs_work: entry\n");

	dprintf(2, "uv__fs_work: looking up resources associated with req %p\n", req);
	store_resources_in_timeout_buf(req);

	if (req->fs_type == UV_FS_CLOSE) {
		dprintf(2, "uv__fs_work: fd %d might be slow (%d) but we'll try to close it anyway\n", req->file, fd_is_slow(req->file));
	}
	else if (are_resources_slow(req)) {
		dprintf(2, "uv__fs_work: resource(s) needed by req %p are slow, returning with req->result ETIMEDOUT\n", req);
		req->result = -ETIMEDOUT;
		return;
	}

  do {
    errno = 0;

#define X(type, action)                                                       \
  case UV_FS_ ## type:                                                        \
    r = action;                                                               \
    break;

    switch (req->fs_type) {
    X(ACCESS, access(req->timeout_buf->path, req->flags));
    X(CHMOD, chmod(req->timeout_buf->path, req->mode));
    X(CHOWN, chown(req->timeout_buf->path, req->uid, req->gid));
    X(CLOSE, uv__fs_close(req));
    X(COPYFILE, uv__fs_copyfile(req));
    X(FCHMOD, fchmod(req->file, req->mode));
    X(FCHOWN, fchown(req->file, req->uid, req->gid));
    X(FDATASYNC, uv__fs_fdatasync(req));
    X(FSTAT, uv__fs_fstat(req->file, req->timeout_buf->statbuf));
    X(FSYNC, uv__fs_fsync(req));
    X(FTRUNCATE, ftruncate(req->file, req->off));
    X(FUTIME, uv__fs_futime(req));
    X(LSTAT, uv__fs_lstat(req->timeout_buf->path, req->timeout_buf->statbuf));
    X(LINK, link(req->timeout_buf->path, req->timeout_buf->new_path));
    X(MKDIR, mkdir(req->timeout_buf->path, req->mode));
    X(MKDTEMP, uv__fs_mkdtemp(req));
    X(OPEN, uv__fs_open(req));
    X(READ, uv__fs_buf_iter(req, uv__fs_read));
    X(SCANDIR, uv__fs_scandir(req));
    X(READLINK, uv__fs_readlink(req));
    X(REALPATH, uv__fs_realpath(req));
    X(RENAME, rename(req->timeout_buf->path, req->timeout_buf->new_path));
    X(RMDIR, rmdir(req->timeout_buf->path));
    X(SENDFILE, uv__fs_sendfile(req));
    X(STAT, uv__fs_stat(req->timeout_buf->path, req->timeout_buf->statbuf));
    X(SYMLINK, symlink(req->timeout_buf->path, req->timeout_buf->new_path));
    X(UNLINK, unlink(req->timeout_buf->path));
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

	dprintf(2, "uv__fs_work: result %ld, sync_timeout_buf'ing\n", req->result);
	sync_timeout_buf(req, r != -1); /* Set read bufs, path, statbuf, etc. */
}


static uint64_t uv__fs_timed_out(struct uv__work* w, void **dat) {
  uv_fs_t* req;

  req = container_of(w, uv_fs_t, work_req);

  dprintf(2, "uv__fs_timed_out: req %p dat %p timed out\n", req, dat);

	/* Propagate to uv__fs_done. */
	req->result = -ETIMEDOUT;

	/* Resource management policy: conservatively mark all resources involved as dangerous. */
  dprintf(2, "uv__fs_timed_out: marking req %p's resources slow\n", req);
	mark_resources_slow(req);

  /* Share timeout_buf with uv__fs_killed for cleanup in the later of uv_fs_req_cleanup, uv__fs_killed. */
	dprintf(2, "uv__fs_timed_out: Adding ref to buf %p\n", req->timeout_buf);
	*dat = req->timeout_buf;
	uv__fs_buf_ref(req->timeout_buf);

  /* Tell threadpool to abort the Task. */
	return 0;
}

#define UV__FS_DONE_INIT(w, status)                     \
  do {                                                  \
		req = container_of(w, uv_fs_t, work_req);           \
		uv__req_unregister(req->loop, req);                 \
																										    \
		if (status == -ECANCELED) {                         \
			assert(req->result == 0);                         \
			req->result = -ECANCELED;                         \
		}                                                   \
		else if (status == -ETIMEDOUT) {                    \
			req->result = -ETIMEDOUT;                         \
		}                                                   \
	}                                                     \
	while(0)

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

  dprintf(2, "uv__fs_done: req %p done, calling cb\n", req);
  req->cb(req);
}

static void uv__fs_done_sync(struct uv__work* w, int status) {
  uv_fs_t* req;

	req = container_of(w, uv_fs_t, work_req);
	/* We never uv__req_init'd (-> uv__req_register), so unlike uv__fs_done, don't unregister. */

	if (status == -ECANCELED) {
		assert(req->result == 0);
		req->result = -ECANCELED;
	}
	else if (status == -ETIMEDOUT) {
		req->result = -ETIMEDOUT;
	}

  dprintf(2, "uv__fs_done_sync: req %p done, post'ing\n", req);
  uv_sem_post(&req->timeout_buf->done);
}


static void uv__fs_killed(void *dat) {
	uv__fs_buf_t *buf;

	buf = (uv__fs_buf_t *) dat;
	if (buf == NULL)
		abort();

	/* Resource management policy:
	 *   The blacklist is permanent, so there's nothing to undo here. */

  /* Calls that touch memory do so in memory stored in the req->timeout_buf for timeout-safety.
	 * The later of us and uv_fs_req_cleanup should clean it up. */
	dprintf(2, "uv__fs_killed: unref'ing buf %p\n", buf);
	uv__fs_buf_unref(buf);
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
	dprintf(2, "uv_fs_close: req %p fd %d\n", req, file);
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
	dprintf(2, "uv_fs_fstat: req %p fd %d sync %i\n", req, file, cb == NULL ? 1 : 0);

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
	dprintf(2, "uv_fs_fstat: req %p path %s sync %i\n", req, path, cb == NULL ? 1 : 0);

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
                  const char* path,
                  uv_fs_cb cb) {
  INIT(MKDTEMP);
	PATH;
  POST;
}


int uv_fs_open(uv_loop_t* loop,
               uv_fs_t* req,
               const char* path,
               int flags,
               int mode,
               uv_fs_cb cb) {
	dprintf(2, "uv_fs_open: req %p path %s sync %i\n", req, path, cb == NULL ? 1 : 0);
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
	int rc;

	dprintf(2, "uv_fs_read: req %p fd %d sync %i\n", req, file, cb == NULL ? 1 : 0);

  INIT(READ);

  if (bufs == NULL || nbufs == 0)
    return -EINVAL;

  req->file = file;

  rc = uv__fs_buf_copy_io_bufs(req->timeout_buf, bufs, nbufs);

  if (rc) {
    if (cb != NULL)
      uv__req_unregister(loop, req);
    return -ENOMEM;
  }

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
	dprintf(2, "uv_fs_realpath: req %p path %p sync %i\n", req, path, cb == NULL ? 1 : 0);

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
  req->timeout_buf->io_bufs[0].len = len;
  POST;
}


int uv_fs_stat(uv_loop_t* loop, uv_fs_t* req, const char* path, uv_fs_cb cb) {
	dprintf(2, "uv_fs_realpath: req %p path %p sync %i\n", req, path, cb == NULL ? 1 : 0);

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
	int rc;
  INIT(WRITE);

  if (bufs == NULL || nbufs == 0)
    return -EINVAL;

  req->file = file;

  rc = uv__fs_buf_copy_io_bufs(req->timeout_buf, bufs, nbufs);
  if (rc) {
    if (cb != NULL)
      uv__req_unregister(loop, req);
    return -ENOMEM;
  }

  req->off = off;
  POST;
}


void uv_fs_req_cleanup(uv_fs_t* req) {

  if (req == NULL)
    return;

	/* Clean up any timeout-safe memory if we have the last reference. */
	if (req->timeout_buf != NULL) {
		dprintf(2, "uv__fs_req_cleanup: unref'ing buf %p\n", req->timeout_buf);
		uv__fs_buf_unref(req->timeout_buf);
		req->timeout_buf = NULL;
	}

  req->path = NULL;

  if (req->fs_type == UV_FS_SCANDIR && req->ptr != NULL)
    uv__fs_scandir_cleanup(req);

/* req->ptr only points to things in req->timeout_buf which we already de-allocated above.
  if (req->ptr != &req->statbuf)
    uv__free(req->ptr);
*/
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
