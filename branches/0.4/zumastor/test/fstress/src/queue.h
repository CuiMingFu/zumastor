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

#define Q_HEAD(Z, TYPE)                              \
	struct { struct TYPE *head, *tail; }

#define Q_ENTRY(TYPE)                                \
	struct { struct TYPE *next, *prev; }

#define Q_INIT(Q) {                                  \
	(Q)->head = (Q)->tail = NULL;                \
}

#define Q_FIRST(Q)                                   \
	(Q)->head

#define Q_LAST(Q, Z)                                 \
	(Q)->tail

#define Q_INSERT_HEAD(Q, X, F) {                     \
	(X)->F.prev = NULL;                          \
	(X)->F.next = (Q)->head;                     \
	if ((Q)->head) {                             \
		(Q)->head->F.prev = (X);             \
	} else {                                     \
		(Q)->tail = (X);                     \
	}                                            \
	(Q)->head = (X);                             \
}

#define Q_INSERT_TAIL(Q, X, F) {                     \
	(X)->F.prev = (Q)->tail;                     \
	(X)->F.next = NULL;                          \
	if ((Q)->tail) {                             \
		(Q)->tail->F.next = (X);             \
	} else {                                     \
		(Q)->head = (X);                     \
	}                                            \
	(Q)->tail = (X);                             \
}

#define Q_REMOVE(Q, X, F) {                          \
	if ((Q)->head == (X)) {                      \
		(Q)->head = (X)->F.next;             \
	}                                            \
	if ((Q)->tail == (X)) {                      \
		(Q)->tail = (X)->F.prev;             \
	}                                            \
	if ((X)->F.prev) {                           \
		(X)->F.prev->F.next = (X)->F.next;   \
	}                                            \
	if ((X)->F.next) {                           \
		(X)->F.next->F.prev = (X)->F.prev;   \
	}                                            \
}

#define Q_FOREACH(X, Q, F)                           \
	for (X = (Q)->head ; X ; X = X->F.next)
