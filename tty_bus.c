#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "configure.h"

#define MAX_TTY        256
#define BUFFER_SIZE    4096
#define POLL_R_TIMEOUT 100
#define POLL_W_TIMEOUT 50
static char *tty_bus_path = NULL;


void exiting(void) {
  unlink(tty_bus_path);
}


void signaled(int signo) {
  exit(0);
}


int bus_init(char *path) {
  struct sockaddr_un sun;
  int connect_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  memset(&sun, 0, sizeof(struct sockaddr_un));
  sun.sun_family = AF_UNIX;
  strncpy(sun.sun_path, path, strlen(path));
  if (bind(connect_fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
    if ((errno == EADDRINUSE)) {
      printf("Could not bind to socket '%s': %s\n", path, strerror(errno));
      exit(-1);
    } else if (bind(connect_fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
      printf("Could not bind to socket '%s' (second attempt): %s", path, strerror(errno));
      exit(-1);
    }
  }
  chmod(sun.sun_path, 0777);
  if (listen(connect_fd, 1) < 0) {
    printf("Could not listen on fd %d: %s", connect_fd, strerror(errno));
    exit(-1);
  }
  return connect_fd;
}


void bus_destroy(int fd, char *path) {
  close(fd);
  unlink(path);
}


static void usage(char *app) {
  fprintf(stderr, "%s, Ver %s.%s.%s\n", basename(app), MAJORV, MINORV, SVNVERSION);
  fprintf(stderr, "Usage: %s [-h] [-s bus_path]\n", app);
  fprintf(stderr, "-h: shows this help\n");
  fprintf(stderr, "-s bus_path: uses bus_path as bus path name (default: /tmp/ttybus)\n");
  exit(2);
}


int prepare_poll(int *tty, struct pollfd **ppfd, int lfd, int flags) {
  struct pollfd *pfd = (struct pollfd *) *ppfd;
  int i, fdcount = 0;
  if (lfd != -1)
    pfd[fdcount++].fd = lfd;

  for (i = 0; i < MAX_TTY; i++) {
    if (tty[i] != -1)
      pfd[fdcount++].fd = tty[i];
  }

  for (i = 0; i < fdcount; i++)
    pfd[i].events = flags;

  return fdcount;
}


void init_dev_array(int **ptty) {
  int i;
  int *tty = *ptty;
  for (i = 0; i < MAX_TTY; i++)
    tty[i] = -1;
}


int check_poll_errors(struct pollfd *pfd, int n, int *tty) {
  int i, j;
  int err = 0;
  for (i = 0; i < n; i++) {
    if (pfd[i].revents & POLLHUP || pfd[i].revents & POLLERR || pfd[i].revents & POLLNVAL) {
      for (j = 0; j < MAX_TTY; j++) {
        if (tty[j] == pfd[i].fd) {
          close(tty[j]);
          tty[j] = -1;
          ++err;
          break;
        }
      }
    }
  }
  return err;
}


void recvbuff(int src, char *buf, int size, int *tty) {
  struct pollfd *wpfd;
  int n, i;
  int pollret;

  wpfd = (struct pollfd *) malloc(sizeof(struct pollfd) * MAX_TTY);
  if (!wpfd)
    fprintf(stderr, "alloc error: %s\n", strerror(errno));


  n = prepare_poll(tty, (struct pollfd **) &wpfd, -1, POLLOUT | POLLHUP);
  printf("Writing to %d clients: %d bytes\n", n, size);
  if (n > 1) {
    pollret = poll(wpfd, n, POLL_W_TIMEOUT);
    if (pollret < 0) {
      fprintf(stderr, "Poll error: %s\n", strerror(errno));
      sleep(1);
      return;
    }
    (void) check_poll_errors(wpfd, n, tty);

    for (i = 0; i < n; i++) {
      if (wpfd[i].revents & POLLOUT && wpfd[i].fd != src)
        write(wpfd[i].fd, buf, size);
    }
  }
  free(wpfd);
}


int main(int argc, char *argv[]) {
  int n = 0;
  int listenfd = -1;
  int i, r = 0;
  int pollret;
  struct pollfd *pfd;
  int *tty;
  char buffer[BUFFER_SIZE];

  pfd = (struct pollfd *) malloc(sizeof(struct pollfd) * (1 + MAX_TTY));
  tty = (int *) malloc(sizeof(int) * MAX_TTY);
  if (!pfd || !tty) {
    fprintf(stderr, "alloc error: %s\n", strerror(errno));
    exit(4);
  }
  while (1) {
    int c;
    c = getopt(argc, argv, "hs:");
    if (c == -1)
      break;

    switch (c) {
      case 'h':
        usage(argv[0]);  // implies exit
        break;
      case 's':
        tty_bus_path = strdup(optarg);
        break;
      default:
        usage(argv[0]);  // implies exit
    }
  }
  if (optind < argc)
    usage(argv[0]);  // implies exit

  if (!tty_bus_path)
    tty_bus_path = strdup("/tmp/ttybus");

  fprintf(stderr, "Creating bus: %s\n", tty_bus_path);

  atexit(exiting);
  sigset(SIGTERM, signaled);
  sigset(SIGINT, signaled);

  init_dev_array((int **) &tty);
  listenfd = bus_init(tty_bus_path);
  if (listenfd < 0) {
    fprintf(stderr, "Cannot bind to %s: %s\n", tty_bus_path, strerror(errno));
    exit(1);
  }
  for (;;) {
    n = prepare_poll(tty, (struct pollfd **) &pfd, listenfd, POLLIN | POLLHUP);
    pollret = poll(pfd, n, POLL_R_TIMEOUT);
    if (pollret < 0) {
      fprintf(stderr, "Poll error: %s, n is %d\n", strerror(errno), n);
      sleep(1);
      continue;
    }
    if (check_poll_errors(pfd, n, tty) > 0)
      continue;

    if (pfd[0].revents & POLLIN) {
      struct sockaddr_un cliaddr;
      socklen_t len = sizeof(struct sockaddr_un);
      int connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &len);
      if (connfd == -1) {
        bus_destroy(listenfd, tty_bus_path);
        listenfd = bus_init(tty_bus_path);
      } else {
        for (i = 0; i < MAX_TTY; i++)
          if (tty[i] == -1) {
            tty[i] = connfd;
            break;
          }
      }
      continue;
    }
    if (pollret > 0) {
      for (;;) {
        if (pfd[i].revents & POLLIN) {
          r = read(pfd[i].fd, buffer, BUFFER_SIZE);
          if (r > 0)
            recvbuff(pfd[i].fd, buffer, r, tty);
          break;
        }
        if (++i >= n)
          i = 1;
      }
    }
  }
}