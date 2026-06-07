#ifndef INDI_PROPERTY_MAPPER_H
#define INDI_PROPERTY_MAPPER_H

#include <string>
#include <memory>
#include <indicom.h>
#include <inditelescope.h>
#include "mount_controller.pb.h"

/**
 * @brief Helper class to map between INDI telescope properties and gRPC messages.
 *
 * Handles conversion of:
 * - INDI equatorial coordinates ↔ gRPC Coordinates
 * - INDI horizontal coordinates ↔ gRPC HorizontalCoordinates
 * - INDI mount position ↔ gRPC MountPosition
 * - INDI telescope state ↔ gRPC ControllerState::MountStatus
 * - INDI pier side ↔ gRPC pier_side
 *
 * Faza 2a: Podstawowe mapowanie dla GotoRaDec, ReadScopeStatus, Park/Unpark.
 * Faza 3+: Rozszerzenie o tracking, guiding, kalibrację.
 */
class IndiPropertyMapper
{
public:
    IndiPropertyMapper(double latitude = 52.0, double longitude = 21.0, double elevation = 100.0);

    /// @brief Set observer location for coordinate conversions.
    void setLocation(double latitude, double longitude, double elevation);

    /// @brief Convert INDI RA/Dec (JNow) to gRPC Coordinates (J2000 with correction flags).
    astro_mount::Coordinates toGrpcCoordinates(double raHours, double decDegrees) const;

    /// @brief Convert gRPC MountPosition to INDI RA/Dec (populates newRA/newDec).
    /// @return true on success.
    bool toIndiRaDec(const astro_mount::MountPosition& mountPos,
                     const astro_mount::ControllerState& state,
                     double lstHours,
                     double& raHours, double& decDegrees) const;

    /// @brief Convert gRPC ControllerState::MountStatus to INDI TrackState.
    int toIndiTrackState(astro_mount::ControllerState::MountStatus status) const;

    /// @brief Convert gRPC pier_side to INDI PierSide.
    int toIndiPierSide(double pierSide) const;

    /// @brief Compute approximate Local Sidereal Time.
    static double computeLst(double longitudeDeg);

    /// @brief Get observer latitude.
    double latitude() const { return latitude_; }

private:
    double latitude_;    // degrees North
    double longitude_;   // degrees East
    double elevation_;   // meters

    /// @brief Convert mount axis positions to equatorial coords (simplified).
    bool mountPositionToRaDec(double axis1Deg, double axis2Deg,
                              double lstHours,
                              double& raHours, double& decDegrees) const;
};

#endif // INDI_PROPERTY_MAPPER_H
