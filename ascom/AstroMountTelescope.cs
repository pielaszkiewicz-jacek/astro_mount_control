using System;
using System.Collections;
using System.Runtime.InteropServices;
using Google.Protobuf.WellKnownTypes;

//
// ASCOM Telescope Driver for AstroMountController
//
// Faza 2a: Podstawowe sterowanie montażem
//   - Connected / Disconnected
//   - SlewToCoordinatesAsync / SlewToTargetAsync
//   - AbortSlew / Stop
//   - Park / Unpark
//   - RightAscension / Declination (from StateCache)
//   - Slewing, Tracking
//   - SideOfPier
//   - SiteLatitude / SiteLongitude / SiteElevation
//   - PulseGuide
//   - SyncToCoordinates
//
// Kolejne fazy dodadzą: MoveAxis, TrackingRate, ephemeris tracking, itd.
//

namespace AstroMount
{
    /// <summary>
    /// ASCOM Telescope driver for the AstroMountController gRPC service.
    /// Implements ITelescopeV3 interface.
    /// 
    /// Connects to the controller via gRPC (typically on a Raspberry Pi)
    /// and translates ASCOM method calls to gRPC RPCs.
    /// 
    /// Connection string format: "host=192.168.1.100;port=50051"
    /// </summary>
    [
        Guid("A1B2C3D4-E5F6-7890-ABCD-EF1234567890"),
        ClassInterface(ClassInterfaceType.None),
        ProgId("AstroMount.Telescope")
    ]
    public class AstroMountTelescope : IDisposable
    {
        // ============================================
        // Internal state
        // ============================================

        private readonly GrpcClient _grpc;
        private readonly StateCache _cache;
        private readonly ConversionHelper _conv;
        private string _host = "localhost";
        private int _port = 50051;
        private MountType _mountType = MountType.Equatorial;
        private bool _disposed;

        // ASCOM target properties
        private double _targetRa;
        private double _targetDec;

        // Tracking state
        private bool _trackingRequested;

        /// <summary>
        /// Create a new AstroMountTelescope driver.
        /// Does NOT connect automatically — call Connected = true to connect.
        /// </summary>
        public AstroMountTelescope()
        {
            _grpc = new GrpcClient(_host, _port);
            _cache = new StateCache(_grpc, pollIntervalMs: 1000);
            _conv = new ConversionHelper();
        }

        // ============================================
        // ASCOM ITelescopeV3 Properties
        // ============================================

        /// <summary>
        /// ASCOM Device Name.
        /// </summary>
        public string Name => "AstroMount Telescope Controller";

        /// <summary>
        /// ASCOM Device Description.
        /// </summary>
        public string Description => "ASCOM Telescope Driver for AstroMountController gRPC service. " +
            "Connects to a mount controller running on a Raspberry Pi (or other Linux host) " +
            "via gRPC protocol. Supports equatorial and alt-az mounts with full slew, " +
            "tracking, park, and guide correction capabilities.";

        /// <summary>
        /// ASCOM Driver Version.
        /// </summary>
        public string DriverVersion => "2.0.0";

        /// <summary>
        /// ASCOM Interface Version (ITelescopeV3 = 3).
        /// </summary>
        public short InterfaceVersion => 3;

        /// <summary>
        /// ASCOM Driver Info.
        /// </summary>
        public string DriverInfo => Description;

        /// <summary>
        /// Connected state — creates or tears down the gRPC connection.
        /// </summary>
        public bool Connected
        {
            get => _grpc.IsConnected;
            set
            {
                if (value)
                {
                    _grpc.Connect();
                    // After connecting, read configuration to update mount type and location
                    try
                    {
                        var config = _grpc.GetConfiguration();
                        _mountType = config.MountType;
                        _conv.SetLatitude(config.Latitude);
                    }
                    catch
                    {
                        // Non-fatal: use defaults
                    }
                }
                else
                {
                    _grpc.Disconnect();
                }
            }
        }

        // ============================================
        // Position properties (from cached state)
        // ============================================

        /// <summary>
        /// Current Right Ascension in hours.
        /// Reads from cached controller state (non-blocking).
        /// </summary>
        public double RightAscension
        {
            get
            {
                var state = _cache.GetState();
                if (state == null) return 0;

                // Compute LST from controller state or approximate from system time
                double lst = ApproximateLst();

                var (ra, _) = _conv.MountPositionToRaDec(
                    state.CurrentPosition, _mountType, lst);
                return ra;
            }
        }

        /// <summary>
        /// Current Declination in degrees.
        /// Reads from cached controller state (non-blocking).
        /// </summary>
        public double Declination
        {
            get
            {
                var state = _cache.GetState();
                if (state == null) return 0;

                double lst = ApproximateLst();
                var (_, dec) = _conv.MountPositionToRaDec(
                    state.CurrentPosition, _mountType, lst);
                return dec;
            }
        }

        /// <summary>
        /// Target Right Ascension in hours (set by SlewToCoordinatesAsync or directly).
        /// </summary>
        public double TargetRightAscension
        {
            get => _targetRa;
            set => _targetRa = value;
        }

        /// <summary>
        /// Target Declination in degrees (set by SlewToCoordinatesAsync or directly).
        /// </summary>
        public double TargetDeclination
        {
            get => _targetDec;
            set => _targetDec = value;
        }

        // ============================================
        // State properties
        // ============================================

        /// <summary>
        /// Whether the mount is currently slewing.
        /// </summary>
        public bool Slewing
        {
            get
            {
                var status = _cache.GetMountStatus();
                return status == ControllerState.Types.MountStatus.Slewing;
            }
        }

        /// <summary>
        /// Whether the mount is currently tracking.
        /// Setting to true resumes tracking of the last target.
        /// Setting to false stops tracking.
        /// </summary>
        public bool Tracking
        {
            get
            {
                var status = _cache.GetMountStatus();
                return status == ControllerState.Types.MountStatus.Tracking;
            }
            set
            {
                if (value && !Tracking)
                {
                    // Resume tracking last target
                    var coords = new Coordinates
                    {
                        Ra = _targetRa,
                        Dec = _targetDec,
                        ApplyPrecession = true,
                        ApplyNutation = true,
                        ApplyRefraction = true
                    };
                    _grpc.TrackObject(coords);
                }
                else if (!value && Tracking)
                {
                    _grpc.Stop();
                }
            }
        }

        /// <summary>
        /// Side of pier (East=1, West=-1).
        /// </summary>
        public PierSide SideOfPier
        {
            get
            {
                var state = _cache.GetState();
                if (state == null) return PierSide.pierWest;
                return state.PierSide > 0 ? PierSide.pierEast : PierSide.pierWest;
            }
        }

        // ============================================
        // Location properties
        // ============================================

        private double _siteLatitude;
        private double _siteLongitude;
        private double _siteElevation;

        /// <summary>
        /// Site latitude in degrees (positive North).
        /// </summary>
        public double SiteLatitude
        {
            get
            {
                try { return _grpc.GetConfiguration().Latitude; }
                catch { return _siteLatitude; }
            }
            set
            {
                _siteLatitude = value;
                try
                {
                    var config = _grpc.GetConfiguration();
                    config.Latitude = value;
                    _grpc.UpdateConfiguration(config);
                    _conv.SetLatitude(value);
                }
                catch { /* will retry on next set */ }
            }
        }

        /// <summary>
        /// Site longitude in degrees (positive East).
        /// </summary>
        public double SiteLongitude
        {
            get
            {
                try { return _grpc.GetConfiguration().Longitude; }
                catch { return _siteLongitude; }
            }
            set
            {
                _siteLongitude = value;
                try
                {
                    var config = _grpc.GetConfiguration();
                    config.Longitude = value;
                    _grpc.UpdateConfiguration(config);
                }
                catch { /* will retry on next set */ }
            }
        }

        /// <summary>
        /// Site elevation in meters.
        /// </summary>
        public double SiteElevation
        {
            get
            {
                try { return _grpc.GetConfiguration().Altitude; }
                catch { return _siteElevation; }
            }
            set
            {
                _siteElevation = value;
                try
                {
                    var config = _grpc.GetConfiguration();
                    config.Altitude = value;
                    _grpc.UpdateConfiguration(config);
                }
                catch { /* will retry on next set */ }
            }
        }

        // ============================================
        // ASCOM ITelescopeV3 Methods
        // ============================================

        /// <summary>
        /// Slew to the specified equatorial coordinates (RA/Dec J2000) and stop.
        /// ASCOM: SlewToCoordinatesAsync — non-blocking slew.
        /// </summary>
        public void SlewToCoordinatesAsync(double raHours, double decDegrees)
        {
            EnsureConnected();

            _targetRa = raHours;
            _targetDec = decDegrees;

            var coords = new Coordinates
            {
                Ra = raHours,
                Dec = decDegrees,
                ApplyPrecession = true,
                ApplyNutation = true,
                ApplyRefraction = true,
                Epoch = 2000.0
            };

            _grpc.SlewToCoordinates(coords);
        }

        /// <summary>
        /// Slew to the current TargetRightAscension and TargetDeclination.
        /// </summary>
        public void SlewToTargetAsync()
        {
            SlewToCoordinatesAsync(_targetRa, _targetDec);
        }

        /// <summary>
        /// Abort the current slew operation (emergency stop).
        /// </summary>
        public void AbortSlew()
        {
            EnsureConnected();
            _grpc.Stop();
        }

        /// <summary>
        /// Park the mount at the configured park position.
        /// </summary>
        public void Park()
        {
            EnsureConnected();
            _grpc.Park();
        }

        /// <summary>
        /// Unpark the mount (wake from park).
        /// </summary>
        public void Unpark()
        {
            EnsureConnected();
            _grpc.Unpark();
        }

        /// <summary>
        /// Send a guide pulse to the mount.
        /// </summary>
        public void PulseGuide(GuideDirection direction, int durationMs)
        {
            EnsureConnected();

            // Convert guide direction and duration to RA/Dec corrections
            // The controller expects corrections in arcseconds.
            // We approximate: at sidereal rate, 15 arcsec/sec = 1 second of RA
            // For a typical guide rate of 0.5x sidereal: 7.5 arcsec/sec
            double guideRateArcsecPerSec = 7.5; // 0.5x sidereal default

            double raCorrection = 0, decCorrection = 0;
            double correctionArcsec = guideRateArcsecPerSec * durationMs / 1000.0;

            switch (direction)
            {
                case GuideDirection.guideNorth:
                    decCorrection = correctionArcsec;
                    break;
                case GuideDirection.guideSouth:
                    decCorrection = -correctionArcsec;
                    break;
                case GuideDirection.guideEast:
                    raCorrection = correctionArcsec;
                    break;
                case GuideDirection.guideWest:
                    raCorrection = -correctionArcsec;
                    break;
            }

            _grpc.Stub.SendGuiderCorrection(new GuiderCorrection
            {
                RaCorrection = raCorrection,
                DecCorrection = decCorrection
            });
        }

        /// <summary>
        /// Synchronize the controller's position to the given coordinates.
        /// Adds a bootstrap measurement and triggers immediate recalibration.
        /// </summary>
        public void SyncToCoordinates(double raHours, double decDegrees)
        {
            EnsureConnected();

            var state = _cache.GetState();
            var mountPos = state?.CurrentPosition ?? new MountPosition();

            var measurement = new BootstrapMeasurement
            {
                Observed = new Coordinates { Ra = raHours, Dec = decDegrees },
                Expected = new Coordinates { Ra = raHours, Dec = decDegrees },
                MountPosition = mountPos,
                UseForInitialAlignment = true
            };

            _grpc.AddBootstrapMeasurement(measurement);
            _grpc.RunBootstrapCalibration();
        }

        // ============================================
        // Required ASCOM methods (minimal stubs)
        // ============================================

        /// <summary>
        /// Supported actions — Faza 3: tpoint_status, temperature, pressure, humidity.
        /// </summary>
        public string[] SupportedActions => new[]
        {
            "tpoint_status",
            "temperature",
            "pressure",
            "humidity",
            "tracking_rate_ra",
            "tracking_rate_dec",
            "guider_status",
            "derotator_status"
        };

        /// <summary>
        /// Execute an action string.
        /// Supported: tpoint_status, temperature, pressure, humidity, tracking_rate_ra/dec, guider_status, derotator_status.
        /// </summary>
        public string Action(string actionName, string actionParameters)
        {
            switch (actionName.ToLowerInvariant())
            {
                case "tpoint_status":
                {
                    var state = _cache.GetState();
                    var tpoint = state?.TpointParams;
                    if (tpoint == null)
                        return "TPOINT: no data available";
                    return $"TPOINT: calibrated={tpoint.Calibrated}, chi2={tpoint.ChiSquared:F6}, " +
                           $"coefficients={string.Join(",", tpoint.Coefficients)}";
                }

                case "temperature":
                {
                    var state = _cache.GetState();
                    return (state?.Temperature ?? 0).ToString("F2");
                }

                case "pressure":
                {
                    var state = _cache.GetState();
                    return (state?.Pressure ?? 0).ToString("F2");
                }

                case "humidity":
                {
                    var state = _cache.GetState();
                    return (state?.Humidity ?? 0).ToString("F2");
                }

                case "tracking_rate_ra":
                {
                    var state = _cache.GetState();
                    return (state?.TrackingRateRa ?? 0).ToString("F6");
                }

                case "tracking_rate_dec":
                {
                    var state = _cache.GetState();
                    return (state?.TrackingRateDec ?? 0).ToString("F6");
                }

                case "guider_status":
                {
                    var state = _cache.GetState();
                    return $"active={state?.GuiderActive ?? false}";
                }

                case "derotator_status":
                {
                    try
                    {
                        var status = _grpc.GetDerotatorStatus();
                        return $"enabled={status.Enabled}, moving={status.Moving}, homed={status.Homed}, " +
                               $"angle={status.CurrentAngle:F2}, rate={status.RotationRate:F6}";
                    }
                    catch
                    {
                        return "derotator: not available";
                    }
                }

                default:
                    throw new NotSupportedException($"Action '{actionName}' is not supported.");
            }
        }

        /// <summary>
        /// Not implemented (always returns null).
        /// </summary>
        public ArrayList SupportedRates => null;

        /// <summary>
        /// Not implemented (returns 0).
        /// </summary>
        public double TrackingRate
        {
            get => 0.015; // sidereal rate in arcsec/sec (approx)
            set { /* TODO: Faza 3 — custom tracking rate */ }
        }

        /// <summary>
        /// Not implemented (returns Sidereal).
        /// </summary>
        public TrackingRates TrackingRates => TrackingRates.TrackingRateSidereal;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public IAxisRates AxisRates(TelescopeAxes axis) => null;

        /// <summary>
        /// Not implemented (returns true if connected).
        /// </summary>
        public bool CanMoveAxis(TelescopeAxes axis) => Connected;

        /// <summary>
        /// Not implemented (returns false).
        /// </summary>
        public bool DoesRefraction => false;

        /// <summary>
        /// Not implemented (returns false).
        /// </summary>
        public bool CanFindHome => false;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanPark => true;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanPulseGuide => true;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSetDeclinationRate => false;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSetGuideRates => false;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSetPark => false;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSetPierSide => false;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSetRightAscensionRate => false;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSetTracking => true;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSlew => true;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSlewAsync => true;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanSync => true;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool CanUnpark => true;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public double DeclinationRate { get => 0; set { } }

        /// <summary>
        /// Not implemented (moved to PulseGuide).
        /// </summary>
        public bool GuideRateDeclination { get => 0.5; set { } }

        /// <summary>
        /// Not implemented (moved to PulseGuide).
        /// </summary>
        public bool GuideRateRightAscension { get => 0.5; set { } }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public double RightAscensionRate { get => 0; set { } }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public double Altitude
        {
            get
            {
                var state = _cache.GetState();
                return state?.CurrentPosition?.Axis2 ?? 0;
            }
        }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public double Azimuth
        {
            get
            {
                var state = _cache.GetState();
                return state?.CurrentPosition?.Axis1 ?? 0;
            }
        }

        /// <summary>
        /// Not implemented (returns false).
        /// </summary>
        public bool AtHome => false;

        /// <summary>
        /// Not implemented (returns true if parked).
        /// </summary>
        public bool AtPark => _cache.GetMountStatus() == ControllerState.Types.MountStatus.Parked;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public bool ApertureDiameter => 0;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public double ApertureArea => 0;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public double FocalLength => 0;

        /// <summary>
        /// Not implemented.
        /// </summary>
        public double EquatorialSystem => 0; // J2000 = 0

        /// <summary>
        /// Not implemented.
        /// </summary>
        public void FindHome() { throw new NotSupportedException(); }

        /// <summary>
        /// Move specified axis at the given rate.
        /// Rate > 0 moves positive direction, rate < 0 negative, rate = 0 stops.
        /// Uses VELOCITY_CONTROL mode via gRPC ControlAxis.
        /// </summary>
        public void MoveAxis(TelescopeAxes axis, double rate)
        {
            EnsureConnected();

            int axisId;
            switch (axis)
            {
                case TelescopeAxes.axisPrimary:   // RA / Az
                    axisId = 0;
                    break;
                case TelescopeAxes.axisSecondary: // Dec / Alt
                    axisId = 1;
                    break;
                case TelescopeAxes.axisTertiary:  // Rotator
                    axisId = 2;
                    break;
                default:
                    throw new InvalidValueException($"Unknown axis: {axis}");
            }

            if (rate == 0.0)
            {
                // Stop the axis
                _grpc.StopAxis(new AxisStopRequest { AxisId = axisId });
            }
            else
            {
                // Velocity control — rate is in deg/s
                _grpc.ControlAxis(new AxisControlRequest
                {
                    AxisId = axisId,
                    Mode = AxisControlRequest.Types.AxisControlMode.VelocityControl,
                    TargetVelocity = rate,
                    Relative = false
                });
            }
        }

        /// <summary>
        /// Set the current position as the park position.
        /// Reads current position via GetState and updates configuration.
        /// </summary>
        public void SetPark()
        {
            EnsureConnected();

            var state = _cache.GetState();
            if (state == null)
                throw new InvalidOperationException("Cannot set park: no controller state available.");

            var config = _grpc.GetConfiguration();
            // Set park position to current mount position (axis1, axis2)
            config.ParkPositionAxis1 = state.CurrentPosition?.Axis1 ?? 0;
            config.ParkPositionAxis2 = state.CurrentPosition?.Axis2 ?? 0;
            _grpc.UpdateConfiguration(config);
        }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public void SlewToAltAzAsync(double altitude, double azimuth)
        {
            EnsureConnected();
            _grpc.SlewToHorizontal(new HorizontalCoordinates
            {
                Altitude = altitude,
                Azimuth = azimuth
            });
        }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public void SlewToAltAz(double altitude, double azimuth)
        {
            SlewToAltAzAsync(altitude, azimuth);
        }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public void SlewToCoordinates(double raHours, double decDegrees)
        {
            SlewToCoordinatesAsync(raHours, decDegrees);
        }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public void SlewToTarget()
        {
            SlewToTargetAsync();
        }

        /// <summary>
        /// Not implemented.
        /// </summary>
        public void SyncToTarget()
        {
            SyncToCoordinates(_targetRa, _targetDec);
        }

        /// <summary>
        /// Not implemented (returns empty).
        /// </summary>
        public ArrayList Rates => null;

        // ============================================
        // Disposal
        // ============================================

        public void Dispose()
        {
            if (!_disposed)
            {
                _cache?.Dispose();
                _grpc?.Dispose();
                _disposed = true;
            }
        }

        // ============================================
        // Helpers
        // ============================================

        private void EnsureConnected()
        {
            if (!Connected)
                throw new InvalidOperationException(
                    "Mount is not connected. Set Connected = true first.");
        }

        /// <summary>
        /// Approximate Local Sidereal Time from system time and longitude.
        /// This is a simplified calculation. The controller performs precise LST internally.
        /// </summary>
        private double ApproximateLst()
        {
            // Julian Date approximation since J2000.0
            DateTime utcNow = DateTime.UtcNow;
            double jd = utcNow.ToOADate() + 2415018.5;
            double jd2000 = jd - 2451545.0;

            // Greenwich Mean Sidereal Time in hours
            double gmst = 18.697374558 + 24.06570982441908 * jd2000;
            gmst = gmst % 24.0;
            if (gmst < 0) gmst += 24.0;

            // Convert to Local Sidereal Time
            double lst = gmst + _siteLongitude / 15.0;
            lst = lst % 24.0;
            if (lst < 0) lst += 24.0;

            return lst;
        }
    }
}
