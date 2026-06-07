using System;
using System.Runtime.InteropServices;
using Google.Protobuf.WellKnownTypes;

//
// ASCOM Rotator Driver for AstroMountController
//
// Faza 5: Osobny sterownik rotatora (derotator).
//   - MoveAbsolute(position)  → ControlFieldRotation(FIXED_ANGLE)
//   - Move(rate)              → ControlFieldRotation(CUSTOM)
//   - Halt()                  → ControlFieldRotation(DISABLED)
//   - Position (get)          → GetDerotatorStatus().current_angle
//   - TargetPosition (get)    → GetDerotatorStatus().target_angle
//   - Home()                  → HomeDerotator()
//   - Connected (get/set)     → gRPC connection management
//

namespace AstroMount
{
    /// <summary>
    /// ASCOM Rotator driver for the AstroMountController derotator.
    /// Implements IRotatorV3 interface.
    ///
    /// Connects to the controller via gRPC (same host:port as telescope driver)
    /// and translates ASCOM Rotator method calls to derotator gRPC RPCs.
    /// </summary>
    [
        Guid("B2C3D4E5-F6A7-8901-BCDE-F12345678901"),
        ClassInterface(ClassInterfaceType.None),
        ProgId("AstroMount.Rotator")
    ]
    public class AstroMountRotator : IDisposable
    {
        // ============================================
        // Internal state
        // ============================================

        private GrpcClient _grpc;
        private string _host = "localhost";
        private int _port = 50051;
        private bool _disposed;

        // Rotator state cache
        private DerotatorStatus _lastStatus;

        /// <summary>
        /// Create a new AstroMountRotator driver.
        /// Does NOT connect automatically — set Connected = true to connect.
        /// </summary>
        public AstroMountRotator()
        {
        }

        /// <summary>
        /// Create with explicit gRPC host/port.
        /// </summary>
        public AstroMountRotator(string grpcHost, int grpcPort)
        {
            _host = grpcHost ?? "localhost";
            _port = grpcPort > 0 ? grpcPort : 50051;
        }

        // ============================================
        // ASCOM IRotatorV3 Properties
        // ============================================

        /// <summary>
        /// ASCOM Device Name.
        /// </summary>
        public string Name => "AstroMount Rotator Controller";

        /// <summary>
        /// ASCOM Device Description.
        /// </summary>
        public string Description => "ASCOM Rotator driver for AstroMountController derotator";

        /// <summary>
        /// ASCOM Driver Version.
        /// </summary>
        public string DriverVersion => "1.0.0";

        /// <summary>
        /// ASCOM Interface Version (Rotator = 3).
        /// </summary>
        public short InterfaceVersion => 3;

        /// <summary>
        /// Connect to the mount controller gRPC service.
        /// Connection string: "host=192.168.1.100;port=50051"
        /// </summary>
        public bool Connected
        {
            get => _grpc?.IsConnected ?? false;
            set
            {
                if (value)
                {
                    if (_grpc == null || !_grpc.IsConnected)
                    {
                        _grpc = new GrpcClient(_host, _port);
                        _grpc.Connect();
                    }
                }
                else
                {
                    _grpc?.Disconnect();
                    _grpc = null;
                }
            }
        }

        /// <summary>
        /// Set up connection and parse host/port from connection string.
        /// Expected format: "host=IP;port=PORT"
        /// </summary>
        public void SetupDialog()
        {
            // In a full implementation, this would show a configuration UI.
            // For now, we use the existing connection or defaults.
            // ASCOM chooser sets up the connection string before creation.
        }

        /// <summary>
        /// Current rotator position in degrees (0-360).
        /// </summary>
        public double Position
        {
            get
            {
                RefreshStatus();
                return _lastStatus?.CurrentAngle ?? 0;
            }
        }

        /// <summary>
        /// Target rotator position in degrees (last MoveAbsolute target).
        /// </summary>
        public double TargetPosition
        {
            get
            {
                RefreshStatus();
                return _lastStatus?.TargetAngle ?? 0;
            }
        }

        /// <summary>
        /// Whether the rotator is currently moving.
        /// </summary>
        public bool Moving
        {
            get
            {
                RefreshStatus();
                return _lastStatus?.Moving ?? false;
            }
        }

        /// <summary>
        /// Whether reverse direction is supported.
        /// </summary>
        public bool CanReverse => false;

        /// <summary>
        /// Whether the rotator is reversed. Not supported.
        /// </summary>
        public bool Reverse { get => false; set { } }

        /// <summary>
        /// ASCOM Device State.
        /// </summary>
        public bool IsMoving => Moving;

        // ============================================
        // ASCOM IRotatorV3 Methods
        // ============================================

        /// <summary>
        /// Move the rotator to an absolute mechanical position (0-360 deg).
        /// </summary>
        public void MoveAbsolute(double position)
        {
            EnsureConnected();
            _grpc.ControlFieldRotation(new FieldRotationControlRequest
            {
                Mode = FieldRotationControlRequest.Types.RotationMode.FixedAngle,
                TargetAngle = position,
                WaitForCompletion = false
            });
        }

        /// <summary>
        /// Move the rotator at the specified rate (deg/s).
        /// Positive = clockwise, negative = counter-clockwise.
        /// </summary>
        public void Move(double rate)
        {
            EnsureConnected();

            if (rate == 0.0)
            {
                // Halt
                Halt();
                return;
            }

            _grpc.ControlFieldRotation(new FieldRotationControlRequest
            {
                Mode = FieldRotationControlRequest.Types.RotationMode.Custom,
                RotationRate = rate,
                Relative = true
            });
        }

        /// <summary>
        /// Immediately stop all rotator motion.
        /// </summary>
        public void Halt()
        {
            EnsureConnected();
            _grpc.ControlFieldRotation(new FieldRotationControlRequest
            {
                Mode = FieldRotationControlRequest.Types.RotationMode.Disabled
            });
        }

        /// <summary>
        /// Home the rotator (find zero position).
        /// </summary>
        public void Home()
        {
            EnsureConnected();
            _grpc.HomeDerotator(new DerotatorHomingRequest
            {
                Method = DerotatorHomingRequest.Types.HomingMethod.Sequential
            });
        }

        // ============================================
        // Disposal
        // ============================================

        public void Dispose()
        {
            if (!_disposed)
            {
                _grpc?.Dispose();
                _grpc = null;
                _disposed = true;
            }
        }

        // ============================================
        // Helpers
        // ============================================

        private void EnsureConnected()
        {
            if (_grpc == null || !_grpc.IsConnected)
                throw new InvalidOperationException(
                    "Rotator is not connected. Set Connected = true first.");
        }

        private void RefreshStatus()
        {
            try
            {
                _lastStatus = _grpc?.GetDerotatorStatus();
            }
            catch
            {
                // Silently keep stale status
            }
        }
    }
}
