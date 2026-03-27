/**
 * LD_PRELOAD wrapper: ensure TCP FIN is sent on close() for socket fds.
 *
 * When the SDK (cvedix) closes the rtmp_des socket without shutdown(SHUT_RDWR),
 * the server may not see a clean FIN. This wrapper intercepts close() and
 * calls shutdown(fd, SHUT_RDWR) before close() for TCP sockets.
 *
 * Usage (worker only, where rtmp_des runs):
 *   LD_PRELOAD=/path/to/libclose_fin.so ./bin/edgeos-worker
 *
 * Or run the whole API with it (worker subprocess will inherit LD_PRELOAD):
 *   LD_PRELOAD=/path/to/libclose_fin.so ./bin/edgeos-api
 *
 * See docs/ZERO_DOWNTIME_ATOMIC_PIPELINE_SWAP_DESIGN.md §12 and support/rtmp_fin_wrapper/README.md
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

static int (*real_close)(int fd) = NULL;

static void init_real_close(void) {
  if (real_close == NULL) {
    real_close = (int (*)(int))dlsym(RTLD_NEXT, "close");
  }
}

int close(int fd) {
  init_real_close();
  if (fd < 0) {
    return real_close ? real_close(fd) : -1;
  }
  int so_type = 0;
  socklen_t len = sizeof(so_type);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &so_type, &len) == 0 &&
      so_type == SOCK_STREAM) {
    shutdown(fd, SHUT_RDWR);
  }
  return real_close ? real_close(fd) : -1;
}
