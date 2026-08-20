#include <cstring>

#include <wasi/api.h>

// wasi-libc probes stdout/stderr before its first write. Puck deliberately
// keeps fd_fdstat_get out of its host ABI, so answer that one local question
// and leave actual bytes on the supported fd_write import.
extern "C" __wasi_errno_t guest_fd_fdstat_get(__wasi_fd_t fd, __wasi_fdstat_t* status)
    __asm__("__imported_wasi_snapshot_preview1_fd_fdstat_get");
extern "C" __wasi_errno_t guest_fd_fdstat_get(__wasi_fd_t fd, __wasi_fdstat_t* status) {
  if (status == nullptr || (fd != 1 && fd != 2)) return __WASI_ERRNO_BADF;
  std::memset(status, 0, sizeof(*status));
  status->fs_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
  status->fs_rights_base = __WASI_RIGHTS_FD_WRITE;
  return __WASI_ERRNO_SUCCESS;
}

// libc++ carries generic descriptor cleanup and seek paths even though this
// reactor opens no files. Resolve those dead-end probes inside the guest so
// the host ABI stays at Puck's four deterministic WASI-lite calls.
extern "C" __wasi_errno_t guest_fd_close(__wasi_fd_t fd)
    __asm__("__imported_wasi_snapshot_preview1_fd_close");
extern "C" __wasi_errno_t guest_fd_close(__wasi_fd_t) { return __WASI_ERRNO_BADF; }

extern "C" __wasi_errno_t guest_fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset,
                                         __wasi_whence_t whence, __wasi_filesize_t* new_offset)
    __asm__("__imported_wasi_snapshot_preview1_fd_seek");
extern "C" __wasi_errno_t guest_fd_seek(__wasi_fd_t, __wasi_filedelta_t, __wasi_whence_t,
                                         __wasi_filesize_t*) {
  return __WASI_ERRNO_BADF;
}
