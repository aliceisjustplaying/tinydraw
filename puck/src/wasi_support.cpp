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
