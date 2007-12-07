/*
 * Copyright (c) 2001 Duke University -- Darrell Anderson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Duke University
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "porting.h"
#include "nfs_constants.h"
#include "my_malloc.h"
#include "report.h"

#define MSG_INITIAL_SIZE 256 /* room enough for most messages */

typedef struct msg {
	char *data;
	int len, maxlen;
} *msg_t;

msg_t
msg_alloc(void)
{
	msg_t msg;
	
	if ((msg = my_malloc(sizeof(struct msg))) == NULL) {
		report_perror(FATAL, "msg_alloc malloc");
		return NULL;
	}
	if ((msg->data = my_malloc(MSG_INITIAL_SIZE)) == NULL) {
		report_perror(FATAL, "msg_alloc malloc");
		my_free(msg);
		return NULL;
	}
	msg->len = 0;
	msg->maxlen = MSG_INITIAL_SIZE;

	return msg;
}

void
msg_free(msg_t msg)
{
	my_free(msg->data);
	my_free(msg);
}

int
msg_resize(msg_t msg, int len)
{
	char *tmp;
	int resized = 0, copylen;

	while (msg->maxlen / 2 > len) {
		msg->maxlen /= 2;
		resized = 1;
	}
	if (msg->maxlen == 0) {
		msg->maxlen = sizeof(int);
		resized = 1;
	}
	while (msg->maxlen < len) {
		msg->maxlen *= 2;
		resized = 1;
	}

	if (resized) {
		if ((tmp = my_malloc(msg->maxlen)) == NULL) {
			report_perror(FATAL, "msg_insert malloc");
			return -1;
		}
		if ((copylen = (len < msg->len) ? len : msg->len) > 0) {
			bcopy(msg->data, tmp, copylen);
		}
		my_free(msg->data);
		msg->data = tmp;
	}
	msg->len = len;

	return 0;
}

int
msg_resize_nocopy(msg_t msg, int len)
{
	assert(len <= msg->maxlen);
	msg->len = len;
	return 0;
}

char *
msg_mtod(msg_t msg)
{
	return msg->data;
}

int
msg_mlen(msg_t msg)
{
	return msg->len;
}

int
msg_insert(msg_t msg, int off, int len, char *data)
{
	if (off == -1) {
		return -1;
	}
	assert(off >= 0 && len >= 0);
	
	if (off + len > msg->maxlen) {
		if (msg_resize(msg, off + len) != 0) {
			return -1;
		}
	}
	bcopy(data, msg->data + off, len);
	if (off + len > msg->len) {
		msg->len = off + len;
	}
	return off + len;
}

int
msg_insert_htonl_int32(msg_t msg, int off, u_int32_t data)
{
	u_int32_t data2 = htonl(data);
	return msg_insert(msg, off, sizeof(u_int32_t), (char *)&data2);
}

int
msg_insert_htonl_int64(msg_t msg, int off, u_int64_t data)
{
	u_int64_t data2;
	txdr_hyper(data, &data2);
	return msg_insert(msg, off, sizeof(u_int64_t), (char *)&data2);
}

int
msg_extract(msg_t msg, int off, int len, char *data)
{
	if (off == -1) {
		return -1;
	}
	assert(off >= 0 && len >= 0);
	if (off + len > msg->len) {
		report_error(NONFATAL, "msg_extract overflow (off=%d len=%d msglen=%d)",
			     off, len, msg->len);
		bzero(data, len);
		return -1;
	}

	bcopy(msg->data + off, data, len);
	return off + len;
}

int
msg_extract_ntohl_int32(msg_t msg, int off, u_int32_t *data)
{
	u_int32_t data2;
	off = msg_extract(msg, off, sizeof(u_int32_t), (char *)&data2);
	*data = ntohl(data2);
	return off;
}

int
msg_extract_ntohl_int64(msg_t msg, int off, u_int64_t *data)
{
	u_int64_t data2;
	off = msg_extract(msg, off, sizeof(u_int64_t), (char *)&data2);
	*data = fxdr_hyper(&data2);
	return off;
}

