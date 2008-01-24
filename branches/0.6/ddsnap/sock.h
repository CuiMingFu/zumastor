#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "trace.h"

#ifndef __DDSNAP_SOCK_H
#define __DDSNAP_SOCK_H
/*
 * Find and return the port number of a host:port pair, updating
 * the length to represent the hostname without the port.
 */
static inline int parse_port(char const *s, unsigned *len)
{
	char const *p = memchr(s, ':', *len);
	if (!p || p == s || p - s == *len)
		return -1;
	*len = p - s;
	return atoi(p + 1);
}

/*
 * Dumbed down interface for listening for IPv4 connections.
 */
static inline int bind_socket(char const *name, unsigned port)
{
	struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
	struct hostent *host;
	int sock;
	int optval;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		error("Can't get socket");
	if (!(host = gethostbyname(name)))
		return -h_errno;
	memcpy(&addr.sin_addr.s_addr, host->h_addr, host->h_length);
	optval = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)); /* failure ok */
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return -errno;
	if (listen(sock, 8) < 0)
		return -errno;
	return sock;
}

/*
 * Dumbed down interface for accepting IPv4 connections.
 */
static inline int accept_socket(int lsock)
{
	struct sockaddr_in addr;
	socklen_t addrlen;
	int csock;

	addrlen = sizeof(addr);
	if ((csock = accept(lsock, (struct sockaddr *)&addr, &addrlen)) < 0)
		return -errno;
	return csock;
}

/*
 * Dumbed down interface for opening an IPv4 connection.
 */
static inline int open_socket(char const *name, unsigned port)
{
	struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
	struct hostent *host;
	int sock;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		error("Can't get socket");
	if (!(host = gethostbyname(name)))
		return -h_errno;
	memcpy(&addr.sin_addr.s_addr, host->h_addr, host->h_length);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return -errno;
	return sock;
}

/*
 * Pass a fd over a local socket connection.  You have to send some stream
 * data as well, just to make an ugly interface even more irritating.
 */
static inline int send_fd(int sock, int fd)
{
	char bogus[]="foo";
	char payload[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_control = payload,
		.msg_controllen = sizeof(payload),
		.msg_iov = &(struct iovec){ .iov_base = bogus, .iov_len = sizeof(bogus) },
		.msg_iovlen = 1,
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

	*cmsg = (struct cmsghdr){ CMSG_LEN(sizeof(int)), SOL_SOCKET, SCM_RIGHTS };
	memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd)); // this is really an array, .cmsg_len gives count (??)

	return sendmsg(sock, &msg, 0) != sizeof(bogus) ? -EIO : 0;
}

static inline int recv_fd(int sock)
{
	char bogus[4];
	char payload[CMSG_SPACE(sizeof(int))];
	struct msghdr msg = {
		.msg_control = payload,
		.msg_controllen = sizeof(payload),
		.msg_iov = &(struct iovec){ .iov_base = bogus, .iov_len = sizeof(bogus) },
		.msg_iovlen = 1,
	};
	struct cmsghdr *cmsg;
	int result;
	int fd;

	result = recvmsg(sock, &msg, 0);

	if (result <= 0)
		return result;
	if (!(cmsg = CMSG_FIRSTHDR(&msg)))
		return -ENODATA;
	if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
                cmsg->cmsg_level != SOL_SOCKET ||
                cmsg->cmsg_type != SCM_RIGHTS)
		return -EBADMSG;

	memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
	return fd;
}
#endif // __DDSNAP_SOCK_H
