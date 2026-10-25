#ifndef ASTRO_MOUNT_CCD_H
#define ASTRO_MOUNT_CCD_H

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <indiccd.h>

#include "CameraGrpcClient.h"

/**
 * @brief INDI CCD driver for the AstroMountController CameraService.
 *
 * Connects to the camera service hosted in-process on the mount controller's
 * unified gRPC port (50051) and exposes it to INDI clients (KStars/Ekos).
 *
 * The gRPC endpoint is read from GRPC_HOST / GRPC_PORT environment variables,
 * mirroring the mount driver (astro_mount_indi_driver).
 */
class AstroMountCCD : public INDI::CCD
{
public:
    AstroMountCCD(const char* grpcHost = "localhost", int grpcPort = 50051);
    virtual ~AstroMountCCD();

    const char* getDefaultName() override;

    void ISGetProperties(const char* dev) override;
    bool initProperties() override;
    bool updateProperties() override;
    bool ISNewNumber(const char* dev, const char* name,
                     double values[], char* names[], int n) override;
    bool ISNewSwitch(const char* dev, const char* name,
                     ISState* states, char* names[], int n) override;
    bool ISNewText(const char* dev, const char* name,
                   char* texts[], char* names[], int n) override;

    bool Connect() override;
    bool Disconnect() override;

    bool StartExposure(float duration) override;
    bool AbortExposure() override;
    int SetTemperature(double temperature) override;

    bool UpdateCCDFrame(int x, int y, int w, int h) override;
    bool UpdateCCDBin(int hor, int ver) override;
    bool UpdateCCDFrameType(INDI::CCDChip::CCD_FRAME fType) override;

    void TimerHit() override;

protected:
    void runExposure(float duration);

    std::unique_ptr<CameraGrpcClient> m_client;
    std::thread m_exposureThread;
    std::atomic<bool> m_exposureAbort{false};
    std::atomic<bool> m_exposureRunning{false};

    std::string m_grpcHost;
    int m_grpcPort{50051};
    bool m_grpcUseSsl{false};

    double m_gain{0.0};
    double m_offset{0.0};
    int m_filterSlot{0};
    std::vector<std::string> m_filterNames;

    // Custom properties (raw INDI vectors, mirroring the mount driver style).
    INumberVectorProperty GainNP{};
    INumber GainN[1]{};
    INumberVectorProperty OffsetNP{};
    INumber OffsetN[1]{};
    INumberVectorProperty FilterSlotNP{};
    INumber FilterSlotN[1]{};
    ITextVectorProperty FilterNameTP{};
    IText FilterNameT[8]{};
};

#endif // ASTRO_MOUNT_CCD_H
