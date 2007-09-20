/* FIXME: there should be an RX and TX entry in this enum
 * for each packet type defined in ../ddsnap/kernel/dm-ddsnap.h,
 * and these enums must match exactly the array event_names in
 * event.c.
 * FIXME: event_hook should be called by outbead() automatically
 * rather than inserting code by hand.
 */
enum event_ids
{
	EVENT_TX_IDENTIFY = 0,
	EVENT_TX_SERVER_READY = 1,
	EVENT_END = 2,
};

int event_parse_options(void);
void event_hook(enum event_ids event_id);
