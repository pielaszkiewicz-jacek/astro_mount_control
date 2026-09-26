#ifndef CANOPEN_CANOPEN_H
#define CANOPEN_CANOPEN_H

/*
 * Minimal CANopen master library (CiA 301) over Linux SocketCAN.
 *
 * Scope:
 *   - SDO expedited read/write (1/2/4 bytes) with retry + abort parsing
 *   - NMT services (start / stop / pre-op / reset node / reset comm)
 *   - Heartbeat / boot-up reception (0x700 + node_id)
 *   - EMCY reception (0x080 + node_id)
 *   - PDO reception passthrough via callback
 *
 * Thread model:
 *   - SDO exchanges are synchronous and serialized with an internal mutex.
 *   - The caller may poll canopen_recv_frame() from its own thread to receive
 *     heartbeat / EMCY / PDO frames; those frames are dispatched to the
 *     registered callbacks.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPEN_MAX_NODES 128

/* COB-ID bases */
#define CANOPEN_COBID_NMT              0x000
#define CANOPEN_COBID_SYNC             0x080
#define CANOPEN_COBID_EMCY_BASE        0x080
#define CANOPEN_COBID_TPDO1_BASE       0x180
#define CANOPEN_COBID_RPDO1_BASE       0x200
#define CANOPEN_COBID_TSDO_BASE        0x580
#define CANOPEN_COBID_RSDO_BASE        0x600
#define CANOPEN_COBID_HEARTBEAT_BASE   0x700

/* SDO command specifiers */
#define CANOPEN_SDO_WRITE_1B           0x2F
#define CANOPEN_SDO_WRITE_2B           0x2B
#define CANOPEN_SDO_WRITE_4B           0x23
#define CANOPEN_SDO_READ_REQ           0x40
#define CANOPEN_SDO_READ_RESP_1B       0x4F
#define CANOPEN_SDO_READ_RESP_2B       0x4B
#define CANOPEN_SDO_READ_RESP_4B       0x43
#define CANOPEN_SDO_WRITE_RESP         0x60
#define CANOPEN_SDO_ABORT              0x80

/* NMT command specifiers */
#define CANOPEN_NMT_START              0x01
#define CANOPEN_NMT_STOP               0x02
#define CANOPEN_NMT_PREOP              0x80
#define CANOPEN_NMT_RESET_NODE         0x81
#define CANOPEN_NMT_RESET_COMM         0x82

/* NMT / heartbeat states */
#define CANOPEN_NMT_STATE_BOOTUP       0x00
#define CANOPEN_NMT_STATE_STOPPED      0x04
#define CANOPEN_NMT_STATE_OPERATIONAL  0x05
#define CANOPEN_NMT_STATE_PRE_OPERATIONAL 0x7F

typedef struct canopen_frame {
    uint32_t cob_id;
    uint8_t  dlc;
    uint8_t  data[8];
} canopen_frame_t;

typedef struct canopen_emergency {
    uint16_t error_code;
    uint8_t  error_register;
    uint8_t  aux[5];
} canopen_emergency_t;

typedef void (*canopen_pdo_cb)(uint32_t cob_id, const uint8_t* data,
                               uint8_t dlc, void* userdata);
typedef void (*canopen_nmt_cb)(uint8_t node_id, uint8_t state, void* userdata);
typedef void (*canopen_emcy_cb)(uint8_t node_id,
                                const canopen_emergency_t* emcy, void* userdata);

typedef struct canopen_ctx {
    int  sock_fd;
    void* mutex;                 /* pthread_mutex_t, opaque to keep header C-safe */
    char iface_name[64];
    uint32_t timeout_us;

    canopen_pdo_cb  pdo_cb;
    void*           pdo_userdata;
    canopen_nmt_cb  nmt_cb;
    void*           nmt_userdata;
    canopen_emcy_cb emcy_cb;
    void*           emcy_userdata;

    canopen_emergency_t last_emergency[CANOPEN_MAX_NODES];
    uint8_t  nmt_state[CANOPEN_MAX_NODES];
} canopen_ctx_t;

/* Lifecycle */
bool canopen_init(canopen_ctx_t* ctx, const char* iface_name, uint32_t timeout_us);
void canopen_shutdown(canopen_ctx_t* ctx);
bool canopen_is_open(const canopen_ctx_t* ctx);

/* Raw frame send / receive */
bool canopen_send_frame(canopen_ctx_t* ctx, uint32_t cob_id,
                        const uint8_t* data, uint8_t dlc);
bool canopen_recv_frame(canopen_ctx_t* ctx, canopen_frame_t* frame);

/* Drains up to max_frames already-buffered frames without blocking and
 * dispatches them to the registered callbacks. Returns the number of frames
 * consumed. Useful for heartbeat/EMCY/PDO monitoring loops. */
int canopen_poll_events(canopen_ctx_t* ctx, int max_frames);

/* SDO expedited */
bool canopen_sdo_write_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t index, uint8_t subindex,
                                 uint32_t value, uint8_t size);
bool canopen_sdo_read_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                uint16_t index, uint8_t subindex,
                                uint32_t* value, uint8_t* size);

/* NMT */
bool canopen_nmt_send(canopen_ctx_t* ctx, uint8_t node_id, uint8_t command);
bool canopen_nmt_set_heartbeat(canopen_ctx_t* ctx, uint8_t node_id,
                               uint16_t heartbeat_ms);

/* Callbacks + cached state */
void canopen_set_pdo_callback(canopen_ctx_t* ctx, canopen_pdo_cb cb, void* userdata);
void canopen_set_nmt_callback(canopen_ctx_t* ctx, canopen_nmt_cb cb, void* userdata);
void canopen_set_emergency_callback(canopen_ctx_t* ctx, canopen_emcy_cb cb, void* userdata);

bool canopen_get_emergency(canopen_ctx_t* ctx, uint8_t node_id,
                           canopen_emergency_t* out);
uint8_t canopen_get_nmt_state(canopen_ctx_t* ctx, uint8_t node_id);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_CANOPEN_H */
