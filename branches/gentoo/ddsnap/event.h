/* Event monitoring / Fault injection
 * This code is solely for testability purposes.
 * It should be disabled when building real releases.
 * Copyright (C) 2007, Google (Dan Kegel)
 * GPL
 */

#ifndef EVENT_H
#define EVENT_H

/* #include "../ddsnap/kernel/dm-ddsnap.h"   // for csnap_codes */

/* Read fault injection options, set globals
 * FIXME: should read from commandline; for now env vars
 *
 * Options:
 * DDSNAP_TRIGGER :== any | packettype from list in dm-ddsnap.h | (kind %d)
 * DDSNAP_COUNT :== %d 
 * DDSNAP_ACTION :== close | abort | (delay_ms %d)
 *
 * TRIGGER defines which packet trigers the action.
 * It can be:
 *  a packet name like IDENTIFY (in which case only that packet matches),
 *  or the word 'any' (in which case any packet matches), 
 *  or the word 'kind' and a number (to match the packet with ID EVENT_FIRST + %d).
 * At the moment, only packet sends cause the action.
 *
 * COUNT says how many triggers per action firing.  e.g. to make
 * the action trigger every other time, use COUNT=2.
 * 
 * ACTION specifies what to do when the above criteria are met.
 * If ACTION is close, the socket is shut down (but not closed)
 * If ACTION is abort, the app is terminated with abort()
 * If ACTION is delay_ms X, event_sleep_ms() is called with the given argument
 * The action is taken immediately before the packet is sent.
 *
 * Examples: 
 * To trigger a 50ms delay every time a packet is sent, use
 *   DDSNAP_ACTION="delay_ms 50" DDSNAP_TRIGGER=any 
 * To abort the app on the 100th packet, use
 *   DDSNAP_ACTION=abort DDSNAP_TRIGGER=any DDSNAP_COUNT=100
 * To close the connection every other time we try to send an IDENTIFY_OK packet, use
 *   DDSNAP_ACTION=close DDSNAP_TRIGGER=identify_ok DDSNAP_COUNT=2
 *
 * Returns 0 on success, -1 on failure.
 */
int event_parse_options(void);

/*
 * Call before sending a packet.
 * Depending on the fault injection options, it might
 * play some dirty trick, like sleeping for five seconds,
 * closing the handle, or aborting the program.
 *
 * outbead() and outhead() will call this for you.  
 * If you send a packet header without them, please also call event_hook.
 * FIXME: need a similar hook for packet reads?
 */
void event_hook(int fd, enum csnap_codes event_id);

#endif
