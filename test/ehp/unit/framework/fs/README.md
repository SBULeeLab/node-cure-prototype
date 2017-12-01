Irrelevant FS APIs:
  close, closeSync: prototype always allows close
  copyFile, copyFileSync: not supported
  createReadStream, etc.: wrapper around lower-level calls
  exists, existsSync: deprecated
  lchmod, lchmodSync: chmod already tested, code paths are the same except for the syscall
  lchown, lchownSync: chown already tested, code paths are the same except for the syscall
  lstat lstatSync: stat already tested, code paths are the same except for the syscall
  mkdtemp, mkdtempSync: mkdir already tested, code paths are the same except for the syscall

Untested because not in deps/uv/src/unix/fs.c:
  watch, watchFile
