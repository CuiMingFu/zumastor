#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h> // read
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <string.h>
//#include <libdlm.h>
#include <popt.h>
#include <signal.h>
#include "dm-ddsnap.h" // message codes
#include "ddsnap.h" // outbead
#include "ddsnap.agent.h"
#include "trace.h"
#include "sock.h" // send_fd, read/writepipe, connect_socket
#include "daemonize.h"

#define trace trace_off

struct client { int sock; enum { CLIENT_CON, SERVER_CON } type; };

static inline int have_server(struct context *context)
{
	if (context->serv == -1)
		return(0);
	return(1);
}

/*
 * The order in which server and client connect to the agent is not fixed,
 * and cannot be fixed because this driver supports server failover on a
 * cluster.  A client may connect before the server has started, or before
 * a server failover has completed.
 *
 * The agent handles this flexible event ordering as follows:
 *
 *  - If a client connects to the agent while a server is connected, the
 *    agent hands a fd for the server socket to the client immediately.
 *
 *  - Otherwise, the client is placed on a list of clients waiting for a
 *    server connection
 *
 * When the server connects to the agent, the agent sends fds for the
 * server socket to any clients waiting for a server connection.
 */

int connect_clients(struct context *context)
{
	warn("connect clients to %s", context->active.address);
	while (context->waiters)
	{
		struct client *client = context->waiting[0];
		int control = client->sock;
		struct server *server = &context->active;
		struct sockaddr_un addr = { .sun_family = server->header.type };
		int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(server->address);
		int sock;

		if (server->header.length > sizeof(addr.sun_path)) // length includes terminating NULL
			error("server socket name, %s, is too long", server->address);
		if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
			error("Can't get socket: %s", strerror(errno)); 
		trace(printf("server socket name: %s\n", server->address););
		strncpy(addr.sun_path, server->address, sizeof(addr.sun_path));
		if (connect(sock, (struct sockaddr *)&addr, addr_len) == -1) {
			// this really sucks: if the address is wrong, we silently wait for
			// some ridiculous amount of time.  Do something about this please.
			warn("Can't connect to server: %s", strerror(errno)); 
			return -1;
		}
		if (outbead(control, CONNECT_SERVER, struct { }) < 0)
			error("Could not send connect message");
		if (send_fd(control, sock) < 0)
			error("Could not pass server connection to target");
		context->waiting[0] = context->waiting[--context->waiters];
		close(sock); 
	}
	return 0;
}

int try_to_instantiate(struct context *context)
{
	warn("Activate local server");
	if (context->active.address)
		free(context->active.address);
	memcpy(&context->active, &context->local, sizeof(struct server));
	if (!(context->active.address = (char *)malloc(context->local.header.length)))
		error("unable to allocate space for socket name\n");
	memcpy(context->active.address, context->local.address, context->local.header.length);
	if (outbead(context->serv, START_SERVER, struct { }) < 0) 
		error("Could not send message to server");
	connect_clients(context);
	return 0;
}

static int incoming(struct context *context, struct client *client)
{
	int err;
	char *err_msg;
	struct messagebuf message;
	int sock = client->sock;
	struct server *server = NULL;

	if ((err = readpipe(sock, &message.head, sizeof(message.head))))
		goto pipe_error;
	if (message.head.length > maxbody)
		goto message_too_long;
	if ((err = readpipe(sock, &message.body, message.head.length)))
		goto pipe_error;

	switch (message.head.code) {
	case SERVER_READY:
		server = &context->local;
		warn("received server ready");
		assert(message.head.length == sizeof(struct server_head));
		memcpy(&server->header, message.body, sizeof(struct server_head));
		if (server->address)
			free(server->address);
		trace(warn("socket name length: %d\n", server->header.length););
		if (!(server->address = (char *)malloc(server->header.length)))
			error("unable to allocate space for server address");
		if ((err = readpipe(sock, server->address, server->header.length)))
			goto pipe_error;
		trace(warn("server socket name %s\n", server->address););
		context->serv = sock; // !!! refuse more than one
		client->type = SERVER_CON;
		goto instantiate;

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
		trace(printf("NEED SERVER is being called\n"););
		if (have_server(context)) {
			trace(printf("Calling connect_clients\n"););
			if (connect_clients(context) == 0)
			    trace(printf("connected\n"););
			break;
		}
		break;

	case CONNECT_SERVER_OK:
		warn("Everything connected properly, all is well");
		break;

	case CONNECT_SERVER_ERROR:
		err = ((struct connect_server_error *)message.body)->err;
		err_msg = ((struct connect_server_error *)message.body)->msg;
		err_msg[message.head.length - (sizeof(err) + 1)] = '\0';
		warn("ERROR: %s, all is NOT well", err_msg);
		break;

	case PROTOCOL_ERROR: 
	{
		struct protocol_error *pe = malloc(message.head.length);

		if (!pe) {
			warn("received protocol error message; unable to retreive information");
			break;
		}

		memcpy(pe, message.body, message.head.length);

		err_msg = "No message sent";
		if (message.head.length - sizeof(struct protocol_error) > 0) {
			pe->msg[message.head.length - sizeof(struct protocol_error) - 1] = '\0';
			err_msg = pe->msg;
		}
		warn("protocol error message - error code: %x unknown code: %x message: %s",
					pe->err, pe->culprit, err_msg);
		free(pe);
		break;
	}
	default:
	{
		uint32_t err = ERROR_UNKNOWN_MESSAGE;
		err_msg = "Agent received unknown message";
		
		warn("Agent received unknown message type %x, length %u", message.head.code, message.head.length);
		if (outhead(sock, PROTOCOL_ERROR, sizeof(struct protocol_error) + strlen(err_msg) +1) < 0 ||
				writepipe(sock, &err, sizeof(uint32_t)) < 0 || 
		 		writepipe(sock, &message.head.code, sizeof(uint32_t)) < 0 ||
				writepipe(sock, err_msg, strlen(err_msg) + 1) < 0) 
			warn("unable to send protocol error message\n");
		break;
	}

	} /* end of switch statement */
	return 0;

instantiate:
	return try_to_instantiate(context);

message_too_long:
	warn("message %x too long (%u bytes)\n", message.head.code, message.head.length);
pipe_error:
	return -1;
}

int monitor_setup(char const *sockname, int *listenfd)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	unsigned int addr_len = sizeof(addr) - sizeof(addr.sun_path) + strlen(sockname);

	if (strlen(sockname) > sizeof(addr.sun_path) - 1)
		error("socket name, %s, is too long", sockname);
	strncpy(addr.sun_path, sockname, sizeof(addr.sun_path));
	unlink(sockname);

	if ((*listenfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		error("unable to obtain socket: %s", strerror(errno));
	if (bind(*listenfd, (struct sockaddr *)&addr, addr_len) == -1)
		error("Can't bind to control socket %s: %s", sockname, strerror(errno));
	if (listen(*listenfd, 5) == -1)
		error("Can't listen on control socket: %s", strerror(errno));

	return 0;
}

/*
 * listenfd is the socket upon which we listen for new connections from
 * clients.
 * getsigfd is a pipe created in daemonize() that allows us to catch the
 * appropriate signals.
 * pollvec[] has the array of fds we're polling.  [0] is listenfd, [1] is
 * getsigfd.  The rest (up to 'clients + 2') are the various clients from
 * which we have accepted connections.  When polling, we can tell which
 * client we're talking to by virtue of the fd that had the event, in terms
 * of its offset in pollvec[].
 */
int monitor(int listenfd, struct context *context, const char *logfile, int getsigfd)
{
	unsigned maxclients = 100, clients = 0, others = 2;
	struct pollfd pollvec[others+maxclients];
	struct client *clientvec[maxclients];

	/* Note that we don't have a server yet. */
	context->serv = -1;

	pollvec[0] = (struct pollfd){ .fd = listenfd, .events = (POLLIN | POLLHUP | POLLERR) };
        pollvec[1] = (struct pollfd){ .fd = getsigfd, .events = POLLIN };

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
			warn("try again");
			connect_clients(context);
			// If we go through this too many times it means somebody
			// out there is sitting on the PW lock but did not write
			// the lvb, this is breakage that should be reported to a
			// human.  So we should do that, but also keep looping
			// forever in case somebody is just being slow or in the
			// process of being fenced/ejected, in which case the PW
			// will eventually come free again.  Yes this sucks.
			continue;
		}

                if (pollvec[1].revents) {
                        u8 sig = 0;
                        /* it's stupid but this read also gets interrupted, so... */
                        do { } while (read(getsigfd, &sig, 1) == -1 && errno == EINTR);              
                        trace_on(warn("Caught signal %i", sig););
                        switch (sig) {
                                case SIGHUP:
                                        fflush(stderr);
                                        fflush(stdout);
                                        re_open_logfile(logfile);
                                        break;
				case SIGTERM:
				case SIGINT:
					exit(0); /* FIXME any cleanup to do here? */
				default:
					warn("I don't handle signal %i", sig);
					break;
                        }
                }

		/*
		 * Accept a new connection.  Allocate a new client; stuff that
		 * into clientvec[] and the new fd into pollvec[] so we can
		 * identify them appropriately below.
		 */
		if (pollvec[0].revents) {
			struct sockaddr_in addr;
			int addr_len = sizeof(addr), sock;

			if (!(sock = accept(listenfd, (struct sockaddr *)&addr, (socklen_t *)&addr_len)))
				error("Cannot accept connection");
			trace_on(warn("Received connection %i", clients););
			assert(clients < maxclients); // !!! make the array bigger

			struct client *client = malloc(sizeof(struct client));
			*client = (struct client){ .sock = sock };
			clientvec[clients] = client;
			pollvec[others+clients] = 
				(struct pollfd){ .fd = sock, .events = (POLLIN | POLLHUP | POLLERR) };
			clients++;
		}
		/* Activity on connection? */
		unsigned i = 0;
		while (i < clients) {
			if (pollvec[others+i].revents) { // !!! check for poll error
				struct client **clientp = clientvec + i, *client = *clientp;

				if (incoming(context, client) == -1) {
					warn("Lost connection %i", i);
					if (client->type == SERVER_CON) {
						warn("local server died...");
						if (!memcmp(&context->active, &context->local, sizeof(struct server))) {
							warn("release lock");
//							struct dlm_lksb *lksb = &context->lksb;
//							memset(&context->active, 0, sizeof(struct server));
//							memset(lksb->sb_lvbptr, 0, sizeof(struct server));
						}
						if (context->local.address)
							free(context->local.address);
						memset(&context->local, 0, sizeof(struct server));
						/* exit now to prevent any potential IO hanging */
						/* FIXME: we would fail over the server here if this was a cluster */
						exit(0);
					}
					close(client->sock);
					free(client);
					--clients;
					clientvec[i] = clientvec[clients];
					pollvec[others + i] = pollvec[others + clients];
//					memmove(clientp, clientp + 1, sizeof(struct client *) * clients);
//					memmove(pollvec + i + others, pollvec + i + others + 1, sizeof(struct pollfd) * clients);
					continue;
				}
			}
			i++;
		}
	}
}
