#include <gtest/gtest.h>
#include "canopen/canopen.h"

#include <cstring>

TEST(CanOpenWrapper, CobIdBases) {
    EXPECT_EQ(0x000u, CANOPEN_COBID_NMT);
    EXPECT_EQ(0x080u, CANOPEN_COBID_SYNC);
    EXPECT_EQ(0x080u, CANOPEN_COBID_EMCY_BASE);
    EXPECT_EQ(0x180u, CANOPEN_COBID_TPDO1_BASE);
    EXPECT_EQ(0x200u, CANOPEN_COBID_RPDO1_BASE);
    EXPECT_EQ(0x580u, CANOPEN_COBID_TSDO_BASE);
    EXPECT_EQ(0x600u, CANOPEN_COBID_RSDO_BASE);
    EXPECT_EQ(0x700u, CANOPEN_COBID_HEARTBEAT_BASE);
}

TEST(CanOpenWrapper, SdoCommandSpecifiers) {
    EXPECT_EQ(0x2Fu, CANOPEN_SDO_WRITE_1B);
    EXPECT_EQ(0x2Bu, CANOPEN_SDO_WRITE_2B);
    EXPECT_EQ(0x23u, CANOPEN_SDO_WRITE_4B);
    EXPECT_EQ(0x40u, CANOPEN_SDO_READ_REQ);
    EXPECT_EQ(0x4Fu, CANOPEN_SDO_READ_RESP_1B);
    EXPECT_EQ(0x4Bu, CANOPEN_SDO_READ_RESP_2B);
    EXPECT_EQ(0x43u, CANOPEN_SDO_READ_RESP_4B);
    EXPECT_EQ(0x60u, CANOPEN_SDO_WRITE_RESP);
    EXPECT_EQ(0x80u, CANOPEN_SDO_ABORT);
}

TEST(CanOpenWrapper, NmtStates) {
    EXPECT_EQ(0x05u, CANOPEN_NMT_STATE_OPERATIONAL);
    EXPECT_EQ(0x7Fu, CANOPEN_NMT_STATE_PRE_OPERATIONAL);
    EXPECT_EQ(0x04u, CANOPEN_NMT_STATE_STOPPED);
}

TEST(CanOpenWrapper, InitFailsOnMissingInterface) {
    canopen_ctx_t ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.sock_fd = -1;
    // "definitely-not-a-real-can-iface-xyz" does not exist.
    EXPECT_FALSE(canopen_init(&ctx, "definitely-not-a-real-can-iface-xyz", 100000));
    EXPECT_FALSE(canopen_is_open(&ctx));
    canopen_shutdown(&ctx); // must be safe on a failed init
}

TEST(CanOpenWrapper, ShutdownOnZeroedContextIsSafe) {
    canopen_ctx_t ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.sock_fd = -1;
    canopen_shutdown(&ctx);
    EXPECT_FALSE(canopen_is_open(&ctx));
}

TEST(CanOpenWrapper, SdoWriteRejectsBadSize) {
    canopen_ctx_t ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.sock_fd = -1;
    EXPECT_FALSE(canopen_sdo_write_expedited(&ctx, 1, 0x200C, 0x02, 1, 3));
}
