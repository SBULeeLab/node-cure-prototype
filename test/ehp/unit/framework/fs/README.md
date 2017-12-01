Irrelevant FS APIs:
  close, closeSync: prototype always allows close
  copyFile, copyFileSync: not supported
  createReadStream, etc.: wrapper around lower-level calls
  exists, existsSync: deprecated

Untested because "uninteresting":
  fdatasync, fdatasyncSync
  fsync, fsyncSync
  futimes, futimesSync
  lchmod, lchmodSync
  lchown, lchownSync
  link, linkSync
  lstat lstatSync
  mkdir, mkdirSync
  mkdtemp, mkdtempSync
  rename, renameSync
  utimes, utimesSync

Untested because not in deps/uv/src/unix/fs.c:
  watch, watchFile

Failures:
  readdirSync: looks like scandir is even less cancelable than is documented, double free in libc?
               run this again after we add cancel masking.
