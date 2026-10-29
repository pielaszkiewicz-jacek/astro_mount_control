using System;
using System.Threading;

namespace AstroMount
{
    /// <summary>
    /// Caches the latest ControllerState by polling GetState() at a fixed interval.
    /// ASCOM properties like RightAscension, Declination, Slewing are read many times
    /// per second by client applications. StateCache ensures reads are instantaneous
    /// while the gRPC call only happens once per second (or configured interval).
    /// 
    /// Thread-safe: reads from _lock-protected field, timer callback writes to it.
    /// </summary>
    public class StateCache : IDisposable
    {
        private readonly GrpcClient _grpc;
        private readonly Timer _pollTimer;
        private readonly int _pollIntervalMs;
        private ControllerState _cachedState;
        private readonly object _lock = new object();
        private DateTime _lastErrorRecovery = DateTime.MinValue;
        private bool _disposed;

        /// <summary>
        /// Create a StateCache that polls the controller every pollIntervalMs.
        /// </summary>
        /// <param name="grpc">Connected GrpcClient instance.</param>
        /// <param name="pollIntervalMs">Polling interval in milliseconds (default 1000ms).</param>
        public StateCache(GrpcClient grpc, int pollIntervalMs = 1000)
        {
            _grpc = grpc ?? throw new ArgumentNullException(nameof(grpc));
            _pollIntervalMs = pollIntervalMs;

            // Start polling immediately; first poll happens on the thread pool
            _pollTimer = new Timer(PollCallback, null, 0, pollIntervalMs);
        }

        /// <summary>
        /// Get the latest cached controller state (non-blocking).
        /// Returns null if no state has been fetched yet.
        /// </summary>
        public ControllerState GetState()
        {
            lock (_lock)
            {
                return _cachedState;
            }
        }

        /// <summary>
        /// Get a specific mount status from the cached state.
        /// Returns Unknown if state is null.
        /// </summary>
        public ControllerState.Types.MountStatus GetMountStatus()
        {
            var state = GetState();
            if (state == null)
                return ControllerState.Types.MountStatus.Unknown;

            return state.Status;
        }

        /// <summary>
        /// Force an immediate poll (blocking).
        /// Useful after issuing a command to get fresh state.
        /// </summary>
        public void Refresh()
        {
            PollCallback(null);
        }

        private void PollCallback(object state)
        {
            if (_disposed)
                return;

            try
            {
                var fresh = _grpc.GetState();
                lock (_lock)
                {
                    _cachedState = fresh;
                }

                // Auto-recover a transient controller ERROR so ASCOM clients stay
                // responsive (mirrors the INDI driver's ClearErrors recovery).
                // Rate-limit to one attempt per 10 s so a real hardware fault is
                // not masked by an endless retry loop.
                if (fresh.Status == ControllerState.Types.MountStatus.Error)
                {
                    DateTime now = DateTime.UtcNow;
                    lock (_lock)
                    {
                        if ((now - _lastErrorRecovery).TotalSeconds < 10)
                            return;
                        _lastErrorRecovery = now;
                    }

                    try
                    {
                        _grpc.ClearErrors();
                    }
                    catch
                    {
                        // Recovery is best-effort; a later tick will retry.
                    }
                }
            }
            catch
            {
                // Silently swallow — stale state will be returned on next read.
                // The timer will retry on the next tick.
                // TODO: add logging if log integration is available
            }
        }

        public void Dispose()
        {
            if (!_disposed)
            {
                _disposed = true;
                _pollTimer?.Dispose();
            }
        }
    }
}
