#define _XOPEN_SOURCE 600 // for posix_memalign()

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include "trace.h"
#include "daemonize.h"

#define trace trace_off

#define BUFSIZE 512

/* Avoid signal races by delivering over a pipe */

static int sigpipe;

void sighandler(int signum)
{
	signal(signum, sighandler);
        trace(printf("caught signal %i\n", signum););
        write(sigpipe, (char[]){signum}, 1);
}

int set_flags(int fd, long args) {
	int mode = fcntl(fd, F_GETFL);

	if (mode < 0) 
		return -errno;
	if (fcntl(fd, F_SETFL, mode | args) < 0)
		return -errno;

	return 0;
}

/*
 * Re-open logfiles on HUP for log rotation
 */
void re_open_logfile(const char *logfile)
{
	if (freopen(logfile, "a", stderr) == NULL)
		error("could not reopen stderr\n");

	dup2(fileno(stderr), fileno(stdout));

	/* O_SYNC is important to avoid deadlock */
	if (set_flags(fileno(stdout), O_SYNC)
			|| set_flags(fileno(stderr), O_SYNC))
		error("unable to set stdout and stderr flags to O_SYNC: %s"
				, strerror(errno));
}

void open_logfile(const char *logfile)
{
	int err;
	char *buffer_stdout, *buffer_stderr;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (freopen(logfile, "a", stderr) == NULL)
		error("could not reopen stderr\n");

	dup2(fileno(stderr), fileno(stdout));

	/*
	 * FIXME: Why are we allocating our own buffers? The OS will
	 *        do this for us. Apparently this was done to troubleshoot
	 *        an issue, but is not needed. We should remove it.
	 *
	 *        Once the buffers go away, we can combine re_open and open
	 *        into one function.
	 */
	if ((err = posix_memalign((void **)&buffer_stdout, BUFSIZE, BUFSIZE)))
  		error("unable to allocate buffer for stdout: %s", strerror(err));
	if ((err = posix_memalign((void **)&buffer_stderr, BUFSIZE, BUFSIZE)))
  		error("unable to allocate buffer for stderr: %s", strerror(err));
	setvbuf(stdout, buffer_stdout, _IOLBF, BUFSIZE);
	setvbuf(stderr, buffer_stderr, _IOLBF, BUFSIZE);

	/* O_SYNC is important to avoid deadlock */
	if (set_flags(fileno(stdout), O_SYNC)
			|| set_flags(fileno(stderr), O_SYNC))
		error("unable to set stdout and stderr flags to O_SYNC: %s"
				, strerror(errno));
}

void write_pidfile(char const *pidfile, pid_t pid)
{
	FILE *fp;

	if (!(fp = fopen(pidfile, "w"))) {
		warn("could not open pid file \"%s\" for writing: %s", pidfile,
			strerror(errno));
	} else {
		if (fprintf(fp, "%lu\n", (unsigned long)pid) < 0)
			warn("could not write pid file \"%s\": %s", pidfile,
				strerror(errno));
		if (fclose(fp) < 0)
			warn("error while closing pid file \"%s\" after writing: %s",
				pidfile, strerror(errno));
	}
}

pid_t daemonize(char const *logfile, char const *pidfile, int *getsigfd)
{
	pid_t pid;
	int pipevec[2];

	/*
	 * Create a pipe for signal handling, to avoid race-conditions
	 */
	if (pipe(pipevec) == -1)
		error("Can't create pipe: %s", strerror(errno));
	sigpipe = pipevec[1];
	*getsigfd = pipevec[0];

	/*
	 * define the signals we care about as a daemon, ignore the rest.
	 */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	fflush(stdout);

	pid = fork();

	if (pid == 0) {
		setpgid(0, 0);

		/* we should close all open file descriptors but the
		 * three standard descriptors should be the only ones
		 * open at this point and they are replaced by freopen */

		chdir("/");

		if (!logfile)
			logfile = "/dev/null";

		if (freopen("/dev/null", "r", stdin) == NULL)
			error("could not reopen stdin\n");

		/*
		 * The following opens both stdout and stderr, so we're
		 * being a good daemon and close/reopening stdin, stdout, and
		 * stderr.
		 */
		open_logfile(logfile);

		if (logfile)
		{
			time_t now;
			char *ctime_str;
		       
			now = time(NULL);
			ctime_str = ctime(&now);
			if (ctime_str[strlen(ctime_str)-1] == '\n')
				ctime_str[strlen(ctime_str)-1] = '\0';

			warn("starting at %s", ctime_str);
		}

		return 0;
	}

	if (pid == -1) {
		int err = errno;

		error("could not fork: %s", strerror(err));
		return -err;
	}

	if (pidfile)
		write_pidfile(pidfile, pid);

	return pid;
}

