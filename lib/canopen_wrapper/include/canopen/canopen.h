#ifndef LIBCANOPEN_CANOPEN_H
#define LIBCANOPEN_CANOPEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================
 *  CAN bus types
 * ======================================================================== */

typedef struct {
    uint32_t can_id;
    uint8_t  can_dlc;
    uint8_t  data[8];
} canopen_frame_t;

/* ========================================================================
 *  CANopen constants
 * ======================================================================== */

/* NMT commands (CiA 301 §7.2.4) */
#define CANOPEN_NMT_START_REMOTE_NODE    0x01
#define CANOPEN_NMT_STOP_REMOTE_NODE     0x02
#define CANOPEN_NMT_ENTER_PREOP          0x80
#define CANOPEN_NMT_RESET_NODE           0x81
#define CANOPEN_NMT_RESET_COMM           0x82

/* NMT state values */
#define CANOPEN_NMT_STATE_BOOTUP         0x00
#define CANOPEN_NMT_STATE_STOPPED        0x04
#define CANOPEN_NMT_STATE_PRE_OPERATIONAL 0x7F
#define CANOPEN_NMT_STATE_OPERATIONAL    0x05

/* CiA 402 Control Word bits */
#define CIA402_CONTROL_SWITCH_ON         0x0001
#define CIA402_CONTROL_ENABLE_VOLTAGE    0x0002
#define CIA402_CONTROL_QUICK_STOP        0x0004
#define CIA402_CONTROL_ENABLE_OPERATION  0x0008
#define CIA402_CONTROL_FAULT_RESET       0x0080

/* CiA 402 Status Word bits */
#define CIA402_STATUS_READY_TO_SWITCH_ON 0x0001
#define CIA402_STATUS_SWITCHED_ON        0x0002
#define CIA402_STATUS_OPERATION_ENABLED  0x0004
#define CIA402_STATUS_FAULT              0x0008
#define CIA402_STATUS_QUICK_STOP         0x0010
#define CIA402_STATUS_VOLTAGE_ENABLED    0x0020
#define CIA402_STATUS_WARNING            0x0080
#define CIA402_STATUS_TARGET_REACHED     0x0400
#define CIA402_STATUS_REMOTE             0x1000

/* CiA 402 modes of operation */
#define CIA402_OPMODE_PROFILE_POS        1
#define CIA402_OPMODE_VELOCITY           2
#define CIA402_OPMODE_PROFILE_VEL        3
#define CIA402_OPMODE_TORQUE             4
#define CIA402_OPMODE_HOMING             6
#define CIA402_OPMODE_INTERPOLATED       7

/* Standard Object Dictionary indices */
#define OD_INDEX_DEVICE_TYPE             0x1000
#define OD_INDEX_ERROR_REGISTER          0x1001
#define OD_INDEX_IDENTITY                0x1018
#define OD_INDEX_COMM_ERROR              0x1003
#define OD_INDEX_COB_ID_SYNC             0x1005
#define OD_INDEX_SYNC_PERIOD             0x1006
#define OD_INDEX_EMERGENCY_COB_ID        0x1014
#define OD_INDEX_HEARTBEAT_TIME          0x1017
#define OD_INDEX_STORE_PARA              0x1010
#define OD_INDEX_RESTORE_PARA            0x1011

/* CiA 402 Profile-specific indices */
#define OD_INDEX_CONTROL_WORD            0x6040
#define OD_INDEX_STATUS_WORD             0x6041
#define OD_INDEX_MODES_OF_OP             0x6060
#define OD_INDEX_MODES_OF_OP_DISPLAY     0x6061
#define OD_INDEX_TARGET_POSITION         0x607A
/* 0x6042 is vl_target_velocity for Velocity mode (mode 2) */
#define OD_INDEX_TARGET_VELOCITY         0x6042
/* 0x60FF is target_velocity for Profile Velocity mode (mode 3) */
#define OD_INDEX_TARGET_VELOCITY_PROFILE 0x60FF
#define OD_INDEX_PROFILE_VELOCITY        0x6081
#define OD_INDEX_PROFILE_ACCEL           0x6083
#define OD_INDEX_PROFILE_DECEL           0x6084
#define OD_INDEX_QUICK_STOP_DECEL        0x6085
#define OD_INDEX_MAX_PROFILE_VELOCITY    0x607F
#define OD_INDEX_MOTION_PROFILE_TYPE     0x6086
#define OD_INDEX_POSITION_RANGE_LIMIT    0x607B
#define OD_INDEX_SOFTWARE_POSITION_LIMIT 0x607D
#define OD_INDEX_HOMING_METHOD           0x6098
#define OD_INDEX_HOMING_SPEEDS           0x6099
#define OD_INDEX_HOMING_ACCEL            0x609A
#define OD_INDEX_SENSOR_CONFIG           0x6097
#define OD_INDEX_FOLLOWING_ERROR_WIN     0x6065
#define OD_INDEX_FOLLOWING_ERROR_TIME    0x6066
#define OD_INDEX_POSITION_ENCODER        0x6381
#define OD_INDEX_VELOCITY_ENCODER        0x6382
#define OD_INDEX_DIGITAL_INPUTS          0x60FD
#define OD_INDEX_DIGITAL_OUTPUTS         0x60FE

/* COB-ID base addresses */
#define CANOPEN_COBID_NMT                0x000
#define CANOPEN_COBID_SYNC               0x080
#define CANOPEN_COBID_EMERGENCY_BASE     0x080 /* + node_id for specific node */
#define CANOPEN_COBID_TPDO1              0x180
#define CANOPEN_COBID_RPDO1              0x200
#define CANOPEN_COBID_TPDO2              0x280
#define CANOPEN_COBID_RPDO2              0x300
#define CANOPEN_COBID_TSDO               0x580
#define CANOPEN_COBID_RSDO               0x600
#define CANOPEN_COBID_HEARTBEAT          0x700

/* SDO commands (CiA 301 §7.2.10) */
#define CANOPEN_SDO_CMD_WRITE_4          0x23
#define CANOPEN_SDO_CMD_WRITE_3          0x27
#define CANOPEN_SDO_CMD_WRITE_2          0x2B
#define CANOPEN_SDO_CMD_WRITE_1          0x2F
#define CANOPEN_SDO_CMD_READ             0x40
#define CANOPEN_SDO_CMD_READ_RESP_4      0x43
#define CANOPEN_SDO_CMD_READ_RESP_3      0x47
#define CANOPEN_SDO_CMD_READ_RESP_2      0x4B
#define CANOPEN_SDO_CMD_READ_RESP_1      0x4F
#define CANOPEN_SDO_CMD_ABORT            0x80

/* SDO abort codes (CiA 301 §7.2.10.7) */
#define CANOPEN_SDO_ABORT_TOGGLE_BIT     0x05030000UL
#define CANOPEN_SDO_ABORT_TIMEOUT        0x05040000UL
#define CANOPEN_SDO_ABORT_CMD_UNKNOWN    0x05040001UL
#define CANOPEN_SDO_ABORT_NO_ACCESS      0x06010000UL
#define CANOPEN_SDO_ABORT_WRITE_ONLY     0x06010001UL
#define CANOPEN_SDO_ABORT_READ_ONLY      0x06010002UL
#define CANOPEN_SDO_ABORT_OD_NO_EXIST    0x06020000UL
#define CANOPEN_SDO_ABORT_PARAM_INCOMP   0x06040043UL
#define CANOPEN_SDO_ABORT_TYPE_MISMATCH  0x06070010UL
#define CANOPEN_SDO_ABORT_DATA_RANGE     0x06090030UL
#define CANOPEN_SDO_ABORT_DATA_TOO_LARGE 0x06090031UL
#define CANOPEN_SDO_ABORT_DATA_TOO_SMALL 0x06090032UL
#define CANOPEN_SDO_ABORT_LOCAL_CTRL     0x08000000UL
#define CANOPEN_SDO_ABORT_DEVICE        0x08000020UL

/* Emergency error codes (CiA 301 §7.2.7) */
#define CANOPEN_EMCY_NO_ERROR            0x0000
#define CANOPEN_EMCY_GENERIC_ERROR       0x1000
#define CANOPEN_EMCY_CURRENT             0x2000
#define CANOPEN_EMCY_VOLTAGE             0x3000
#define CANOPEN_EMCY_TEMPERATURE         0x4000
#define CANOPEN_EMCY_COMMUNICATION       0x5000
#define CANOPEN_EMCY_DEVICE_PROFILE      0x6000
#define CANOPEN_EMCY_MANUFACTURER        0xFF00

/* ========================================================================
 *  Opaque handle
 * ======================================================================== */

typedef struct canopen_ctx canopen_ctx_t;

/* ========================================================================
 *  Callback types
 * ======================================================================== */

typedef void (*canopen_pdo_cb_t)(void* userdata, uint32_t cob_id,
                                 const uint8_t* data, uint8_t dlc);
typedef void (*canopen_nmt_cb_t)(void* userdata, uint8_t node_id,
                                 uint8_t state);
typedef void (*canopen_emcy_cb_t)(void* userdata, uint8_t node_id,
                                  uint16_t error_code,
                                  const uint8_t* frame_data,
                                  uint8_t frame_dlc);

/* ========================================================================
 *  Core API
 * ======================================================================== */

canopen_ctx_t* canopen_create(void);
void canopen_destroy(canopen_ctx_t* ctx);

bool canopen_init(canopen_ctx_t* ctx, const char* interface,
                  int node_id, int baud_rate);
void canopen_shutdown(canopen_ctx_t* ctx);

bool canopen_is_connected(canopen_ctx_t* ctx);
int  canopen_get_node_id(canopen_ctx_t* ctx);

/* ========================================================================
 *  NMT (Network Management)
 * ======================================================================== */

bool canopen_nmt_send_command(canopen_ctx_t* ctx, uint8_t node_id,
                              uint8_t command);
uint8_t canopen_nmt_get_state(canopen_ctx_t* ctx, uint8_t node_id);
bool canopen_nmt_set_hb_consumer(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t heartbeat_ms);

/* ========================================================================
 *  SDO (Service Data Object) — expedited transfer
 * ======================================================================== */

bool canopen_sdo_write_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                 uint16_t index, uint8_t subindex,
                                 const void* data, size_t len);

bool canopen_sdo_read_expedited(canopen_ctx_t* ctx, uint8_t node_id,
                                uint16_t index, uint8_t subindex,
                                void* data, size_t* len);

/* ========================================================================
 *  CiA 402 — Drive profile
 * ======================================================================== */

bool canopen_402_set_control_word(canopen_ctx_t* ctx, uint8_t node_id,
                                  uint16_t cw);
uint16_t canopen_402_get_status_word(canopen_ctx_t* ctx, uint8_t node_id);
bool canopen_402_enable_drive(canopen_ctx_t* ctx, uint8_t node_id);
bool canopen_402_disable_drive(canopen_ctx_t* ctx, uint8_t node_id);
bool canopen_402_quick_stop(canopen_ctx_t* ctx, uint8_t node_id);
bool canopen_402_fault_reset(canopen_ctx_t* ctx, uint8_t node_id);
bool canopen_402_set_mode(canopen_ctx_t* ctx, uint8_t node_id, int8_t mode);
bool canopen_402_set_target_position(canopen_ctx_t* ctx, uint8_t node_id,
                                     int32_t position);

/* ========================================================================
 *  Raw frame I/O
 * ======================================================================== */

bool canopen_send_frame(canopen_ctx_t* ctx, const canopen_frame_t* frame);
int  canopen_recv_frame(canopen_ctx_t* ctx, canopen_frame_t* frame,
                        int timeout_ms);

/* ========================================================================
 *  PDO / NMT callbacks
 * ======================================================================== */

void canopen_set_pdo_callback(canopen_ctx_t* ctx, canopen_pdo_cb_t cb,
                              void* userdata);
void canopen_set_nmt_callback(canopen_ctx_t* ctx, canopen_nmt_cb_t cb,
                              void* userdata);
void canopen_set_emergency_callback(canopen_ctx_t* ctx, canopen_emcy_cb_t cb,
                                    void* userdata);
bool canopen_get_emergency(canopen_ctx_t* ctx, uint8_t node_id,
                           uint16_t* error_code, uint8_t* error_register);

#ifdef __cplusplus
}
#endif

#endif /* LIBCANOPEN_CANOPEN_H */
