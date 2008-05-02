#define PACKED __attribute__ ((packed))

struct head
{
	uint32_t code;
	uint32_t length;
};

enum {
	REPLY_ERROR = 0xbead0000,
	NEED_SERVER,
	CONNECT_SERVER,
	REPLY_CONNECT_SERVER,
	SERVER_READY,
	START_SERVER,
	SHUTDOWN_SERVER,
	CONTROL_SOCKET,
	IDENTIFY,
	REPLY_IDENTIFY,
	REQUEST_WRITE,
	RELEASE_WRITE,
	GRANT_SYNCED,
	GRANT_UNSYNCED,
	ADD_UNSYNCED,
	DEL_UNSYNCED,
	DRAIN_REGION,
	SET_HIGHWATER,
	SYNC_REGION,
	REGION_SYNCED,
	PAUSE_REQUESTS,
	RESUME_REQUESTS,
	BOUNCE_REQUEST,
};

typedef unsigned long region_t;

struct identify { uint32_t id; } PACKED;
struct region_message { region_t regnum; } PACKED;
struct reply_identify { unsigned region_bits; } PACKED;

/* decruft me... !!! */
#define maxbody 500
struct messagebuf { struct head head; char body[maxbody]; };
/* ...decruft me */

// bios submitted before server arrives must be split conservatively (see "bogus")
#define MIN_REGION_BITS 12
