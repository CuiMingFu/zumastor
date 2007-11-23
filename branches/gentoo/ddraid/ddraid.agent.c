#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h> // read
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <netinet/in.h>
//#include <libdlm.h>
#include <popt.h>
#include "dm-ddraid.h" // message codes
#include "ddraid.h" // outbead
#include "trace.h"
#include "sock.h" // send_fd, read/writepipe, connect_socket

#define trace trace_on

struct client { int sock; };

struct context {
	struct server active, local;
	int serv;
	int waiters;
	struct client *waiting[100];
	int polldelay;
	unsigned ast_state;
};

static inline int have_address(struct server *server)
{
	return !!server->address_len;
}

static int connect_clients(struct context *context)
{
	warn("connect clients to %x", *(int *)(context->active.address));
	while (context->waiters)
	{
		struct client *client = context->waiting[0];
		int control = client->sock;
		struct server *server = &context->active;
		struct sockaddr_in addr = { .sin_family = server->type, .sin_port = server->port };
		int sock;

		if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			error("Can't get socket");
		memcpy(&addr.sin_addr.s_addr, server->address, server->address_len);
		if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			// this really sucks: if the address is wrong, we silently wait for
			// some ridiculous amount of time.  Do something about this please.
			warn("Can't connect to server");
			return -1;
		}
		if (outbead(control, CONNECT_SERVER, struct { }) < 0)
			error("Could not send connect message");
		if (send_fd(control, sock) < 0)
			error("Could not pass server connection to target");
		context->waiting[0] = context->waiting[--context->waiters];
	}
	return 0;
}

static int try_to_instantiate(struct context *context)
{
	warn("Activate local server");
	memcpy(&context->active, &context->local, sizeof(struct server));
	if (outbead(context->serv, START_SERVER, struct { }) < 0)
		error("Could not send message to server");
	connect_clients(context);
	return 0;
}

static int incoming(struct context *context, struct client *client)
{
	int err;
	struct messagebuf message;
	int sock = client->sock;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
	case SERVER_READY:
		warn("received server ready");
		assert(message.head.length == sizeof(struct server));
		memcpy(&context->local, message.body, sizeof(struct server));
		context->serv = sock; // !!! refuse more than one
		try_to_instantiate(context);
		break;

	case NEED_SERVER:
		context->waiting[context->waiters++] = client;
		/*
		 * If we have a local server, try to instantiate it as the master.
		 * If there's already a master out there, connect to it.  If there
		 * was a master but it went away then the exclusive lock is up for
		 * grabs.  Always ensure the exclusive is still there before by
		 * trying to get it, before relying on the lvb server address,
		 * because that could be stale.
		 *
		 * If there's no local server, don't do anything: instantiation
		 * will be attempted when/if the local server shows up.
		 */
		if (have_address(&context->active))
			connect_clients(context);
		else if (have_address(&context->local))
			try_to_instantiate(context);
		break;
	case REPLY_CONNECT_SERVER:
		warn("Everything connected properly, all is well");
		break;
	default: 
		warn("Unknown message %x", message.head.code);
		break;
	}
	return 0;

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
pipe_error:
	return -1;
}

static int monitor(int nobg, char const *sockname, struct context *context)
{
	unsigned maxclients = 100, clients = 0, others = 1;
	struct pollfd pollvec[others+maxclients];
	struct client clientvec[maxclients];
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);
	int listener = socket(AF_UNIX, SOCK_STREAM, 0);

	assert(listener >= 0);
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	if (sockname[0] == '@')
		addr.sun_path[0] = 0;
	else
		unlink(sockname);

	if (bind(listener, (struct sockaddr *)&addr, addr_len) == -1)
		error("Can't bind to control socket %s: %s", sockname, strerror(errno));
	if (listen(listener, 5) == -1)
		error("Can't listen to control socket: %s", strerror(errno));

	if (!nobg)
		if (daemon(0, 0) == -1)
			error("Can't start daemon, %s (%i)", strerror(errno), errno);

	pollvec[0] = (struct pollfd){ .fd = listener, .events = POLLIN };
	assert(pollvec[0].fd > 0);

	while (1) {
		switch (poll(pollvec, others+clients, context->polldelay)) {
		case -1:
			if (errno == EINTR)
				continue;
			error("poll failed, %s", strerror(errno));
		case 0:
			/* Timeouts happen here */
			context->polldelay = -1;
			warn("failed to get lvb");
			try_to_instantiate(context);
			// If we go through this too many times it means somebody
			// out there is sitting on the PW lock but did not write
			// the lvb, this is breakage that should be reported to a
			// human.  So we should do that, but also keep looping
			// forever in case somebody is just being slow or in the
			// process of being fenced/ejected, in which case the PW
			// will eventually come free again.  Yes this sucks.
			continue;
		}

		/* New connection? */
		if (pollvec[0].revents) {
			struct sockaddr_in addr;
			int addr_len = sizeof(addr), sock;

			if (!(sock = accept(listener, (struct sockaddr *)&addr, &addr_len)))
				error("Cannot accept connection");
			trace_on(warn("Client %i connected", clients););
			assert(clients < maxclients); // !!! make the array bigger
			pollvec[others+clients] = (struct pollfd){ .fd = sock, .events = POLLIN };
			clientvec[clients] = (struct client){ .sock = sock };
			clients++;
		}

		/* Client activity? */
		unsigned i = 0;
		while (i < clients) {
			if (pollvec[others+i].revents) { // !!! check for poll error
				if (incoming(context, clientvec + i) == -1) {
					warn("Client %i disconnected", i);
					close(clientvec[i].sock);
					memmove(clientvec + i, clientvec + i + 1,
						sizeof(struct client) * --clients);
					continue;
				}
			}
			i++;
		}
	}
}

int main(int argc, char *argv[])
{
        poptContext optCon;
	char c;
        int nobg=0;
        char const *sockname;

        struct poptOption optionsTable[] = {
                { "foreground", 'f', POPT_ARG_NONE, &nobg, 0, "do not daemonize server", NULL },
                POPT_AUTOHELP
                POPT_TABLEEND
        };

        optCon = poptGetContext(NULL, argc, (char const **)argv, optionsTable, 0);
        poptSetOtherOptionHelp(optCon, "<agent_socket>");

	while ((c = poptGetNextOpt(optCon)) >= 0);
	if (c < -1) {
		fprintf(stderr, "%s: %s: %s\n", argv[0], poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));
		return 1;
	}

        sockname = poptGetArg(optCon);
        if (sockname == NULL) {
                fprintf(stderr, "%s: socket name for ddraid agent must be specified\n", argv[0]);
                poptPrintUsage(optCon, stderr, 0);
                return 1;
        }
        if (poptPeekArg(optCon) != NULL) {
                fprintf(stderr, "%s: only one socket name may be specified\n", argv[0]);
                poptPrintUsage(optCon, stderr, 0);
                return 1;
        }

        poptFreeContext(optCon);

        if (monitor(nobg, sockname, &(struct context){ .polldelay = -1 }) < 0)
                error("Could not start ddraid agent server\n");

        return 0; /* not reached */
}

