using System;
using System.Threading.Tasks;
using Grpc.Core;
using Google.Protobuf.WellKnownTypes;

namespace AstroMount
{
    /// <summary>
    /// gRPC client wrapper for MountControllerService.
    /// Manages channel lifecycle and exposes all RPC methods.
    /// 
    /// Faza 1 (Fundament): Connect, Disconnect, GetState, CheckHealth, ClearErrors.
    /// Faza 2 (Podstawowe sterowanie): SlewToCoordinates, SlewToHorizontal, TrackObject,
    ///     Stop, Park, Unpark, GetConfiguration, UpdateConfiguration,
    ///     AddBootstrapMeasurement, RunBootstrapCalibration.
    /// </summary>
    public class GrpcClient : IDisposable
    {
        private readonly string _host;
        private readonly int _port;
        private Channel _channel;
        private MountControllerService.MountControllerServiceClient _stub;
        private bool _disposed;

        /// <summary>Connection state.</summary>
        public bool IsConnected => _channel?.State == ConnectivityState.Ready;

        /// <summary>Underlying gRPC channel (exposed for advanced scenarios).</summary>
        public Channel Channel => _channel;

        /// <summary>Underlying service stub (exposed for advanced scenarios).</summary>
        public MountControllerService.MountControllerServiceClient Stub => _stub;

        public GrpcClient(string host = "localhost", int port = 50051)
        {
            _host = host ?? throw new ArgumentNullException(nameof(host));
            _port = port;
        }

        /// <summary>
        /// Create gRPC channel and verify connectivity via CheckHealth.
        /// Throws on failure.
        /// </summary>
        public void Connect()
        {
            if (IsConnected)
                return;

            _channel = new Channel($"{_host}:{_port}", ChannelCredentials.Insecure);
            _stub = new MountControllerService.MountControllerServiceClient(_channel);

            // Verify connectivity with a health check
            try
            {
                var health = _stub.CheckHealth(new HealthCheckRequest { Service = "mount_controller" });
                if (health.Status != HealthCheckResponse.Types.ServingStatus.Serving)
                {
                    throw new InvalidOperationException(
                        $"MountController gRPC service is not serving. Status: {health.Status}");
                }
            }
            catch
            {
                // Clean up on failure
                _channel.ShutdownAsync().Wait();
                _channel = null;
                _stub = null;
                throw;
            }
        }

        /// <summary>
        /// Gracefully disconnect gRPC channel.
        /// </summary>
        public void Disconnect()
        {
            if (_channel == null)
                return;

            try
            {
                _channel.ShutdownAsync().Wait(TimeSpan.FromSeconds(5));
            }
            catch
            {
                // Forceful shutdown on timeout
                _channel.ShutdownAsync().Wait();
            }
            finally
            {
                _channel = null;
                _stub = null;
            }
        }

        /// <summary>
        /// Reconnect: disconnect then connect again.
        /// </summary>
        public void Reconnect()
        {
            Disconnect();
            Connect();
        }

        // ============================================
        // Faza 1: Podstawowe metody
        // ============================================

        /// <summary>
        /// Get current controller state.
        /// </summary>
        public ControllerState GetState()
        {
            EnsureConnected();
            return _stub.GetState(new Empty());
        }

        /// <summary>
        /// Check if the controller gRPC service is healthy.
        /// </summary>
        public HealthCheckResponse CheckHealth(HealthCheckRequest request = null)
        {
            EnsureConnected();
            return _stub.CheckHealth(request ?? new HealthCheckRequest
            {
                Service = "mount_controller"
            });
        }

        /// <summary>
        /// Clear any error state on the controller.
        /// </summary>
        public void ClearErrors()
        {
            EnsureConnected();
            _stub.ClearErrors(new Empty());
        }

        // ============================================
        // Faza 2: Podstawowe sterowanie montażem
        // ============================================

        /// <summary>
        /// Slew telescope to equatorial coordinates (RA/Dec J2000).
        /// </summary>
        public void SlewToCoordinates(Coordinates coords)
        {
            EnsureConnected();
            _stub.SlewToCoordinates(coords);
        }

        /// <summary>
        /// Slew telescope to horizontal coordinates (Altitude/Azimuth).
        /// </summary>
        public void SlewToHorizontal(HorizontalCoordinates coords)
        {
            EnsureConnected();
            _stub.SlewToHorizontal(coords);
        }

        /// <summary>
        /// Start tracking an object at the given equatorial coordinates.
        /// </summary>
        public void TrackObject(Coordinates coords)
        {
            EnsureConnected();
            _stub.TrackObject(coords);
        }

        /// <summary>
        /// Stop all mount motion immediately.
        /// </summary>
        public void Stop()
        {
            EnsureConnected();
            _stub.Stop(new Empty());
        }

        /// <summary>
        /// Park the mount at the configured park position.
        /// </summary>
        public void Park()
        {
            EnsureConnected();
            _stub.Park(new Empty());
        }

        /// <summary>
        /// Unpark the mount (wake from park).
        /// </summary>
        public void Unpark()
        {
            EnsureConnected();
            _stub.Unpark(new Empty());
        }

        /// <summary>
        /// Get current controller configuration (location, mount parameters, etc.).
        /// </summary>
        public Configuration GetConfiguration()
        {
            EnsureConnected();
            return _stub.GetConfiguration(new Empty());
        }

        /// <summary>
        /// Update controller configuration.
        /// </summary>
        public void UpdateConfiguration(Configuration config)
        {
            EnsureConnected();
            _stub.UpdateConfiguration(config);
        }

        // ============================================
        // Faza 2: Bootstrap calibration
        // ============================================

        /// <summary>
        /// Add a bootstrap measurement for initial alignment.
        /// </summary>
        public void AddBootstrapMeasurement(BootstrapMeasurement measurement)
        {
            EnsureConnected();
            _stub.AddBootstrapMeasurement(measurement);
        }

        /// <summary>
        /// Run bootstrap calibration with collected measurements.
        /// </summary>
        public BootstrapCalibrationResult RunBootstrapCalibration()
        {
            EnsureConnected();
            return _stub.RunBootstrapCalibration(new Empty());
        }

        /// <summary>
        /// Get bootstrap calibration status.
        /// </summary>
        public BootstrapStatus GetBootstrapStatus()
        {
            EnsureConnected();
            return _stub.GetBootstrapStatus(new Empty());
        }

        /// <summary>
        /// Clear all collected bootstrap measurements.
        /// </summary>
        public void ClearBootstrapMeasurements()
        {
            EnsureConnected();
            _stub.ClearBootstrapMeasurements(new Empty());
        }

        // ============================================
        // Faza 3: Axis control
        // ============================================

        /// <summary>
        /// Control a specific mount axis (position or velocity).
        /// </summary>
        public void ControlAxis(AxisControlRequest request)
        {
            EnsureConnected();
            _stub.ControlAxis(request);
        }

        /// <summary>
        /// Stop a specific axis.
        /// </summary>
        public void StopAxis(AxisStopRequest request)
        {
            EnsureConnected();
            _stub.StopAxis(request);
        }

        /// <summary>
        /// Emergency stop (all axes or specific axis).
        /// </summary>
        public void EmergencyStop(EmergencyStopRequest request)
        {
            EnsureConnected();
            _stub.EmergencyStop(request);
        }

        // ============================================
        // Faza 3: Guider connection & corrections
        // ============================================

        /// <summary>
        /// Connect the internal guider.
        /// </summary>
        public void ConnectGuider(GuiderConfig config)
        {
            EnsureConnected();
            _stub.ConnectGuider(config);
        }

        /// <summary>
        /// Disconnect the internal guider.
        /// </summary>
        public void DisconnectGuider()
        {
            EnsureConnected();
            _stub.DisconnectGuider(new Empty());
        }

        /// <summary>
        /// Send a guider correction pulse.
        /// </summary>
        public void SendGuiderCorrection(GuiderCorrection correction)
        {
            EnsureConnected();
            _stub.SendGuiderCorrection(correction);
        }

        // ============================================
        // Faza 5: Derotator / Field Rotation
        // ============================================

        /// <summary>
        /// Enable or disable field rotation compensation.
        /// </summary>
        public void EnableFieldRotation(FieldRotationParams request)
        {
            EnsureConnected();
            _stub.EnableFieldRotation(request);
        }

        /// <summary>
        /// Control field rotation (position, rate, mode).
        /// </summary>
        public void ControlFieldRotation(FieldRotationControlRequest request)
        {
            EnsureConnected();
            _stub.ControlFieldRotation(request);
        }

        /// <summary>
        /// Get current derotator status.
        /// </summary>
        public DerotatorStatus GetDerotatorStatus()
        {
            EnsureConnected();
            return _stub.GetDerotatorStatus(new Empty());
        }

        /// <summary>
        /// Home the derotator (find zero position).
        /// </summary>
        public void HomeDerotator(DerotatorHomingRequest request)
        {
            EnsureConnected();
            _stub.HomeDerotator(request);
        }

        // ============================================
        // Helper methods
        // ============================================

        private void EnsureConnected()
        {
            if (_stub == null || _channel == null)
                throw new InvalidOperationException(
                    "gRPC client is not connected. Call Connect() first.");
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                Disconnect();
                _disposed = true;
            }
        }
    }
}
