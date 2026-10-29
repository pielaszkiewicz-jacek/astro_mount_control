using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Windows.Forms;
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

        private GrpcClient _grpc;
        private StateCache _cache;
        private readonly ConversionHelper _conv;
        private string _host = "localhost";
        private int _port = 50051;
        private bool _useSsl;
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

        /// <summary>
        /// ASCOM setup model — a dedicated setup dialog (ActionSetup) is
        /// available; the connection string is also supported by the chooser.
        /// </summary>
        public int SetupDialogType => 2; // SetupDialogType.Setup

        /// <summary>
        /// Connection string: "host=192.168.1.100;port=50051" (port optional,
        /// "ssl=1"/"tls=1" optional). Parsed on set; the gRPC client is
        /// recreated for the new endpoint.
        /// </summary>
        public string ConnectionString
        {
            get => $"host={_host};port={_port}{( _useSsl ? ";ssl=1" : "" )}";
            set
            {
                ApplyConnectionString(value);
                RecreateClient();
            }
        }

        /// <summary>
        /// Chooser action (ASCOM) — returns false because no chooser dialog is
        /// implemented; the connection string is applied on set.
        /// </summary>
        public bool ActionChoose(string dialogId) => false;

        /// <summary>
        /// Setup dialog action (ASCOM) — shows a Windows Forms dialog where the
        /// user configures the gRPC host/port/TLS and can test the connection.
        /// </summary>
        public void ActionSetup()
        {
            using (var dialog = new SetupDialog(_host, _port, _useSsl))
            {
                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    _host = dialog.Host;
                    _port = dialog.Port;
                    _useSsl = dialog.UseSsl;
                    RecreateClient();
                }
            }
        }

        /// <summary>
        /// Recreate the gRPC client and state cache for the current endpoint.
        /// </summary>
        private void RecreateClient()
        {
            _cache.Dispose();
            _grpc.Dispose();
            _grpc = new GrpcClient(_host, _port, _useSsl);
            _cache = new StateCache(_grpc, pollIntervalMs: 1000);
        }

        /// <summary>
        /// Parse a connection string of the form "host=...;port=..." into the
        /// internal host/port/ssl fields. Missing keys keep current values.
        /// </summary>
        private void ApplyConnectionString(string value)
        {
            if (string.IsNullOrWhiteSpace(value)) return;
            foreach (var part in value.Split(';'))
            {
                var kv = part.Split(new[] { '=' }, 2);
                if (kv.Length != 2) continue;
                var key = kv[0].Trim().ToLowerInvariant();
                var val = kv[1].Trim();
                if (key == "host" && val.Length > 0) _host = val;
                else if (key == "port" && int.TryParse(val, out var p)) _port = p;
                else if ((key == "ssl" || key == "tls")) _useSsl = val == "1" || val.ToLowerInvariant() == "true";
            }
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
                        _siteLatitude = config.Latitude;
                        _siteLongitude = config.Longitude;
                        _siteElevation = config.Altitude;
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

                var (raJNow, decJNow) = GetCurrentRaDec(state);
                var (raJ2000, _) = _conv.JNowToJ2000(raJNow, decJNow);
                return raJ2000;
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

                var (raJNow, decJNow) = GetCurrentRaDec(state);
                var (_, decJ2000) = _conv.JNowToJ2000(raJNow, decJNow);
                return decJ2000;
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
        /// Setting to true resumes tracking at the current position.
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
                    // Track the CURRENT on-sky position, not the last Goto target.
                    // The controller works in the apparent (JNow) frame.
                    var state = _cache.GetState() ?? _grpc.GetState();
                    var (raJNow, decJNow) = GetCurrentRaDec(state);
                    var coords = new Coordinates
                    {
                        Ra = raJNow,
                        Dec = decJNow,
                        Epoch = 0.0,
                        TrackingMode = 0 // SIDEREAL
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
                if (state == null) return PierSide.pierUnknown;

                // gRPC pier_side: 1 = East, -1 = West, 0 = unknown (mirrors INDI).
                if (state.PierSide > 0) return PierSide.pierEast;
                if (state.PierSide < 0) return PierSide.pierWest;
                return PierSide.pierUnknown;
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

            // ASCOM supplies J2000 coordinates. The controller's slew/track
            // pipeline works in the apparent (JNow) frame and does NOT apply
            // precession itself, so precess before sending (mirrors the INDI
            // driver passing JNow through unchanged).
            _targetRa = raHours;
            _targetDec = decDegrees;

            var (raNow, decNow) = _conv.J2000ToJNow(raHours, decDegrees);
            var coords = new Coordinates
            {
                Ra = raNow,
                Dec = decNow,
                Epoch = 0.0
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
            double guideRateArcsecPerSec = 0.5 * 15.041067; // 0.5x sidereal (arcsec/s)

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

            var state = _cache.GetState() ?? _grpc.GetState();

            // The server treats MountPosition.axis1/axis2 as TELESCOPE degrees
            // (servo ÷ gear ratio). current_position holds RAW servo degrees and
            // would corrupt the bootstrap SVD fit — use telescope axes instead
            // (this mirrors the INDI driver's addSyncMeasurement).
            var mountPos = new MountPosition
            {
                Axis1 = state?.TelescopeAxis1 ?? 0,
                Axis2 = state?.TelescopeAxis2 ?? 0
            };

            // Expected catalog coordinates: ASCOM supplies J2000, the controller
            // works in JNow, so precess the catalog position to the apparent frame.
            var (raExpectedJNow, decExpectedJNow) = _conv.J2000ToJNow(raHours, decDegrees);
            var expected = new Coordinates
            {
                Ra = raExpectedJNow,
                Dec = decExpectedJNow,
                Epoch = 0.0
            };

            // Observed: the telescope's current on-sky pointing (JNow).
            var (raObserved, decObserved) = GetCurrentRaDec(state);
            var observed = new Coordinates
            {
                Ra = raObserved,
                Dec = decObserved,
                Epoch = 0.0
            };

            var measurement = new BootstrapMeasurement
            {
                Observed = observed,
                Expected = expected,
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
        public bool CanSetPark => true;

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
                if (state == null) return 0;

                var (raJNow, decJNow) = GetCurrentRaDec(state);
                var (alt, _) = _conv.RaDecToHorizontal(raJNow, decJNow, ApproximateLst());
                return alt;
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
                if (state == null) return 0;

                var (raJNow, decJNow) = GetCurrentRaDec(state);
                var (_, az) = _conv.RaDecToHorizontal(raJNow, decJNow, ApproximateLst());
                return az;
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
        /// Return the telescope's current on-sky position in the apparent (JNow)
        /// frame. Prefers the controller's corrected current_ra/current_dec, and
        /// falls back to telescope axes for older controllers.
        /// </summary>
        private (double RaHours, double DecDegrees) GetCurrentRaDec(ControllerState state)
        {
            if (state == null)
                return (0, 0);

            // current_ra/current_dec are the corrected on-sky position (JNow).
            if (state.CurrentRa != 0.0 || state.CurrentDec != 0.0)
            {
                double ra = state.CurrentRa % 24.0;
                if (ra < 0) ra += 24.0;
                return (ra, state.CurrentDec);
            }

            // Fallback: telescope axes are TELESCOPE degrees (servo ÷ gear ratio),
            // already normalized by the controller. current_position holds RAW
            // servo degrees and must NOT be used here.
            double haDeg = state.TelescopeAxis1;
            double decDeg = state.TelescopeAxis2;
            if (decDeg > 180.0) decDeg -= 360.0;

            double haHours = haDeg / 15.0;
            if (haHours > 12.0) haHours -= 24.0;
            else if (haHours < -12.0) haHours += 24.0;

            double raHours = ApproximateLst() - haHours;
            raHours = ((raHours % 24.0) + 24.0) % 24.0;
            return (raHours, decDeg);
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
