#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "configure.h"

#define MAX_TTY        256
#define BUFFER_SIZE    4096
#define POLL_R_TIMEOUT 100
#define POLL_W_TIMEOUT 50
#define POLL_INTERVAL  10000000

static char *ttyfake;
static char *tty_bus_path;
static char *ttybak;
static int force_overwrite = 0;
static int restore = 0;


static void usage(char *app) {
  fprintf(stderr, "%s, Ver %s.%s.%s\n", basename(app), MAJORV, MINORV, SVNVERSION);
  fprintf(stderr, "Usage: %s [-h] [-s bus_path] tty_device\n", app);
  fprintf(stderr, "-h: shows this help\n");
  fprintf(stderr, "-d: detach from terminal and run as daemon\n");
  fprintf(stderr, "-s bus_path: uses bus_path as bus path name (default: /tmp/ttybus)\n");
  fprintf(stderr, "-o: temporarly backup tty_device to tty_device.bak, if it exists, and restore the original file at exit\n\n");
  fprintf(stderr, "Please also see: tty_bus, tty_attach, tty_plug, dpipe\n");
  fprintf(stderr, "Example of usage:\n");
  fprintf(stderr, "  Create a new bus called /tmp/ttyS0mux\n");
  fprintf(stderr, "    tty_bus -d -s /tmp/ttyS0mux\n");
  fprintf(stderr, "  Connect a real device to the bus /tmp/ttyS0mux\n");
  fprintf(stderr, "    tty_attach -d -s /tmp/ttyS0mux /dev/ttyS0\n");
  fprintf(stderr, "  Create two fake ttyS0 devices, attached to the bus /tmp/ttyS0mux\n");
  fprintf(stderr, "    tty_fake -d -s /tmp/ttyS0mux /dev/ttyS0.0\n");
  fprintf(stderr, "    tty_fake -d -s /tmp/ttyS0mux /dev/ttyS0.1\n");
  exit(2);
}


int tty_connect(char *path) {
  struct sockaddr_un sun;
  int connect_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  memset(&sun, 0, sizeof(struct sockaddr_un));
  sun.sun_family = AF_UNIX;
  strncpy(sun.sun_path, path, strlen(path));
  if (connect(connect_fd, (struct sockaddr *) &sun, sizeof(sun)) < 0) {
    perror("Cannot connect to socket");
    syslog(LOG_ERR, "Cannot connect to socket\n");
    exit(-1);
  }
  return connect_fd;
}

static void _tty_cleanup(void) {
  fprintf(stderr, "Removing symlink\n");
  syslog(LOG_INFO, "Removing symlink\n");
  unlink(ttyfake);
  if(restore) {
    fprintf(stderr, "Restoring original device\n");
    syslog(LOG_INFO, "Restoring original device\n");
    link(ttybak, ttyfake);
    unlink(ttybak);
  }
}


static void tty_restore(int __attribute__((unused)) signo) {
  exit(0);
}

int main(int argc, char *argv[])
{
  int fd; 
  struct pollfd pfd[2];
  int pollret, r;
  char buffer[BUFFER_SIZE];
  char *pts;
  int ptmx;
  int daemonize = 0;
  struct timespec poll_interval, remaining;
  poll_interval.tv_sec = 0;
  poll_interval.tv_nsec = POLL_INTERVAL;

  while (1) {
    int c;
    c = getopt(argc, argv, "dhos:");
    if (c == -1)
      break;

    switch (c) {
      case 'd':
        daemonize = 1;
        break;

      case 'h':
        usage(argv[0]); // implies exit
        break;

      case 's':
        tty_bus_path = strdup(optarg);
        break;

      case 'o':
        force_overwrite = 1;
        break;

      default:
        usage(argv[0]); // implies exit
    }
  }

  if (optind != (argc - 1))
    usage(argv[0]);  // implies exit

  if (daemonize)
    daemon(0, 0);

  ttyfake = strdup(argv[optind]);
  if (access(ttyfake, W_OK) == 0) {
    if (force_overwrite) {
      ttybak = malloc(strlen(ttyfake) + 5);
      sprintf(ttybak, "%s.bak", ttyfake);
      unlink(ttybak);
      link(ttyfake, ttybak);
      unlink(ttyfake);
      restore = 1;
    } else {
      fprintf(stderr, "%s already exists! use -o to force overwrite\n", ttyfake);
      syslog(LOG_ERR, "%s already exists! use -o to force overwrite\n", ttyfake);
      exit(1);
    }
  }
    

  if (!tty_bus_path)
    tty_bus_path = strdup("/tmp/ttybus");
  
  fprintf(stderr, "Connecting to bus: %s\n", tty_bus_path);
  syslog(LOG_INFO, "Connecting to bus: %s\n", tty_bus_path);
  fd = tty_connect(tty_bus_path);


  ptmx = open("/dev/ptmx", O_RDWR);
  pts = (char *) ptsname(ptmx);
  fprintf(stderr, "Device: %s is now %s\n", pts, ttyfake);
  syslog(LOG_INFO, "Device: %s is now %s\n", pts, ttyfake);
  grantpt(ptmx);
  unlockpt(ptmx);

  symlink(pts, ttyfake);
  chmod(pts, 00777);
  atexit(_tty_cleanup);
  sigset(SIGTERM, tty_restore);
  sigset(SIGINT, tty_restore);
  sigset(SIGUSR1, tty_restore);
  sigset(SIGUSR2, tty_restore);

  for (;;) {
    nanosleep(&poll_interval, &remaining);
    pfd[0].fd = ptmx;
    pfd[0].events = POLLIN;
    pfd[1].fd = fd;
    pfd[1].events = POLLIN;
    pollret = poll(pfd, 2, 1000);
    if (pollret < 0) {
      fprintf(stderr, "Poll error: %s\n", strerror(errno));
      syslog(LOG_ERR, "Poll error: %s\n", strerror(errno));
      exit(1);
    }
    if (pollret == 0) {
      continue;
    }

    if ((pfd[0].revents & POLLERR || pfd[0].revents & POLLNVAL) ||
        (pfd[1].revents & POLLHUP || pfd[1].revents & POLLERR || pfd[1].revents & POLLNVAL)) {
      syslog(LOG_INFO, "Terminating: %d %d\n", pfd[0].revents, pfd[1].revents);
      exit(1);
    }
    if (pfd[0].revents & POLLIN) {
      r = read(ptmx, buffer, BUFFER_SIZE);
      pfd[1].events = POLLOUT;
      pollret = poll(&pfd[1], 1, 50);
      if (pollret < 0) {
        fprintf(stderr, "Poll error: %s\n", strerror(errno));
        syslog(LOG_ERR, "Poll error: %s\n", strerror(errno));
        exit(1);
      }
      if (pfd[1].revents & POLLOUT) {
        write(fd, buffer, r);
      }
    }
    if (pfd[1].revents & POLLIN) {
      r = read(fd, buffer, BUFFER_SIZE);
      pfd[0].fd = ptmx;
      pfd[0].events = POLLOUT;
      pollret = poll(&pfd[0], 1, 50);
      if (pollret < 0) {
        fprintf(stderr, "Poll error: %s\n", strerror(errno));
        syslog(LOG_ERR, "Poll error: %s\n", strerror(errno));
        exit(1);
      }
      if (pfd[0].revents & POLLOUT) {
        write(ptmx, buffer, r);
      }
    }
  }
}
