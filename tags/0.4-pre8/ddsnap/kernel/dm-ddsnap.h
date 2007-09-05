#define PACKED __attribute__ ((packed))
#define MAGIC  0xadbe

struct head { uint32_t code; uint32_t length; } PACKED;

enum csnap_codes
{
	PROTOCOL_ERROR = 0xbead0000,
	IDENTIFY,
	IDENTIFY_OK,
	IDENTIFY_ERROR,
	QUERY_WRITE,
	ORIGIN_WRITE_OK,
	ORIGIN_WRITE_ERROR,
	SNAPSHOT_WRITE_OK,
	SNAPSHOT_WRITE_ERROR,
	QUERY_SNAPSHOT_READ,
	SNAPSHOT_READ_OK,
	SNAPSHOT_READ_ERROR,
	SNAPSHOT_READ_ORIGIN_OK,
	SNAPSHOT_READ_ORIGIN_ERROR,
	FINISH_SNAPSHOT_READ,
	CREATE_SNAPSHOT, 
	CREATE_SNAPSHOT_OK, 
	CREATE_SNAPSHOT_ERROR, 
	DELETE_SNAPSHOT, 
	DELETE_SNAPSHOT_OK,
	DELETE_SNAPSHOT_ERROR,
	DUMP_TREE,
	INITIALIZE_SNAPSTORE,
	NEED_SERVER,
	CONNECT_SERVER,
	CONNECT_SERVER_OK,
	CONNECT_SERVER_ERROR,
	CONTROL_SOCKET,
	SERVER_READY,
	START_SERVER,
	SHUTDOWN_SERVER,
	SET_IDENTITY,
	UPLOAD_LOCK,
	FINISH_UPLOAD_LOCK,
	NEED_CLIENTS, /* unused */
	UPLOAD_CLIENT_ID,
	FINISH_UPLOAD_CLIENT_ID,
	REMOVE_CLIENT_IDS,
	LIST_SNAPSHOTS,
	SNAPSHOT_LIST,
	PRIORITY,
	PRIORITY_OK, 
	PRIORITY_ERROR, 
	USECOUNT,
	USECOUNT_ERROR,
	USECOUNT_OK,
	STREAM_CHANGELIST,
	STREAM_CHANGELIST_OK, 
	STREAM_CHANGELIST_ERROR, 
	SEND_DELTA,
	SEND_DELTA_PROCEED,
	SEND_DELTA_DONE, 
	SEND_DELTA_ERROR,
	STATUS,
	STATUS_OK,
	STATUS_ERROR,
	REQUEST_SNAPSHOT_STATE,
	SNAPSHOT_STATE,
	REQUEST_ORIGIN_SECTORS,
	ORIGIN_SECTORS,
};

enum csnap_error_codes
{
	ERROR_REFUSED = 0xdead0001,
	ERROR_SIZE_MISMATCH,
	ERROR_OFFSET_MISMATCH,
	ERROR_INVALID_SNAPSHOT,
	ERROR_PRIORITY,
	ERROR_USECOUNT,
	ERROR_UNKNOWN_MESSAGE,
	ERROR_OTHER,
};

#define ID_BITS 16

struct protocol_error { uint32_t err; uint32_t culprit; char msg[]; } PACKED;
struct usecount_info { uint32_t snap; int32_t usecnt_dev; } PACKED;
struct usecount_ok { uint16_t usecount; } PACKED;
struct usecount_error { uint32_t err; char msg[]; } PACKED;
struct priority_info { uint32_t snap; int8_t prio; } PACKED;
struct priority_ok { int8_t prio; } PACKED;
struct priority_error { uint32_t err; char msg[]; } PACKED;
struct match_id { uint64_t id; uint64_t mask; } PACKED;
struct set_id { uint64_t id; } PACKED;
struct identify { uint64_t id; uint32_t snap; uint64_t off; uint64_t len; } PACKED; // off, len are in sectors  
struct identify_ok { uint32_t chunksize_bits; } PACKED;
struct identify_error { uint32_t err; char msg[]; } PACKED; // !!! why not use reply_error and include msg
struct connect_server_error { uint32_t err; char msg[]; } PACKED; // !!! why not use reply_error and include msg
struct create_snapshot { uint32_t snap; } PACKED;
struct generate_changelist { uint32_t snap1; uint32_t snap2; } PACKED;
struct snapinfo { uint32_t snap; int8_t prio; uint16_t usecnt; char zero[3]; uint64_t ctime; } PACKED;
struct snaplist { uint32_t count; struct snapinfo snapshots[]; } PACKED;
struct stream_changelist { uint32_t snap1; uint32_t snap2; } PACKED;
struct changelist_stream { uint64_t chunk_count; uint32_t chunksize_bits; } PACKED;
struct status_request { uint32_t snap; } PACKED;
struct overall_status { uint32_t chunksize_bits; uint64_t used; uint64_t free; } PACKED;
struct status { uint64_t ctime; uint32_t snap; uint64_t chunk_count[]; } PACKED;
struct status_message { uint64_t ctime; struct overall_status meta; struct overall_status store; uint32_t write_density; uint32_t status_count; uint32_t num_columns; char status_data[]; } PACKED;
struct state_message {uint32_t snap; uint32_t state; } PACKED;
struct origin_sectors { uint64_t count; } PACKED;

typedef uint16_t shortcount; /* !!! what is this all about */

struct rw_request
{
	uint32_t id;
	shortcount count;
	struct chunk_range
	{
		uint64_t chunk;
		shortcount chunks;
	} PACKED ranges[];
} PACKED;

/* !!! can there be only one flavor of me please */
struct rw_request1
{
	uint32_t id;
	shortcount count;
	struct chunk_range ranges[1];
} PACKED;

/* decruft me... !!! */
#define maxbody 500
struct rwmessage { struct head head; struct rw_request body; };
struct messagebuf { struct head head; char body[maxbody]; };
/* ...decruft me */