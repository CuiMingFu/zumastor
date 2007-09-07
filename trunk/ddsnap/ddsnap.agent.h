struct context {
	struct server active, local;
	int serv;
	int waiters;
	struct client *waiting[100];
	int polldelay;
	unsigned ast_state;
};

int monitor_setup(char const *sockname, int *listenfd);
int monitor(int listenfd, struct context *context, const char *logfile, int getsigfd);
