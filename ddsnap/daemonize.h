#ifndef DAEMONIZE_H

#define DEAMONIZE_H
#include <unistd.h>

extern pid_t daemonize(const char *logfile, const char *pidfile, int *getsigfd);
extern void re_open_logfile(const char *pidfile);
extern void sighandler(int signum);
void setup_signals(int *getsigfd);

#endif
