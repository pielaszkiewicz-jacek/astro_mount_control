# Astronomical Mount Controller - Web Interface

A modern web-based interface for controlling astronomical mounts via gRPC API. This application provides full telescope control including slewing, tracking, calibration, and monitoring through an intuitive web interface.

## Features

- **Full Telescope Control**: Slew to coordinates, track objects, park/unpark, emergency stop
- **Real-time Monitoring**: Live mount status, position tracking, temperature, and performance metrics
- **TPOINT Calibration**: Add measurements, run calibration, view TPOINT parameters
- **Autoguider Integration**: Connect/disconnect guider, send corrections
- **Sky Map Visualization**: Interactive sky map showing mount position and target
- **Configuration Management**: Load/save mount configuration parameters
- **System Health Monitoring**: CPU usage, memory usage, connection status
- **Comprehensive Logging**: Real-time system logs with filtering and clearing
- **Responsive Design**: Works on desktop and mobile devices

## Architecture

```
┌─────────────────────────────────────────────┐
│            Web Browser Interface            │
│  (HTML/CSS/JavaScript - Single Page App)   │
└──────────────────────┬──────────────────────┘
                       │ HTTP/JSON (REST API)
┌──────────────────────▼──────────────────────┐
│       HTTP/JSON to gRPC Proxy Server        │
│        (Node.js Express - port 8080)        │
└──────────────────────┬──────────────────────┘
                       │ gRPC (protobuf)
┌──────────────────────▼──────────────────────┐
│      Astronomical Mount Controller          │
│          (C++ gRPC Server - port 50051)     │
└─────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

1. **Mount Controller Server**: Must be running on `localhost:50051`
2. **Node.js**: Version 14 or higher
3. **npm**: Node package manager

### Installation

1. **Install proxy server dependencies**:
   ```bash
   cd web/proxy
   npm install
   ```

2. **Start the proxy server**:
   ```bash
   cd web/proxy
   npm start
   ```
   The proxy server will start on port 8080.

3. **Start the mount controller**:
   Ensure the Astronomical Mount Controller gRPC server is running on port 50051.

4. **Open the web interface**:
   Open your browser and navigate to:
   ```
   http://localhost:8080
   ```

### Development Mode

For development with automatic restart:

```bash
cd web/proxy
npm run dev
```

## File Structure

```
web/
├── index.html                    # Main HTML file
├── README.md                     # This file
├── static/                       # Static assets
│   ├── css/
│   │   └── style.css            # Main stylesheet
│   ├── js/
│   │   ├── api-client.js        # API client for communication
│   │   ├── app.js               # Main application logic
│   │   └── sky-map.js           # Sky map visualization
│   └── images/                   # Image assets
├── proto/                        # Protobuf definitions
│   └── mount_controller.proto   # gRPC service definition
└── proxy/                        # HTTP/JSON to gRPC proxy
    ├── server.js                # Proxy server implementation
    ├── package.json             # Node.js dependencies
    └── proxy.log                # Server logs (auto-generated)
```

## API Endpoints

The proxy server provides the following REST API endpoints:

### Health & Status
- `GET /api/health` - Check system health and connection status
- `GET /api/state` - Get current mount state

### Mount Control
- `POST /api/slew` - Slew to coordinates
- `POST /api/track` - Start tracking object
- `POST /api/stop` - Stop mount movement
- `POST /api/park` - Park mount
- `POST /api/unpark` - Unpark mount
- `POST /api/home` - Move to home position

### Configuration
- `GET /api/configuration` - Get current configuration
- `PUT /api/configuration` - Update configuration

### TPOINT Calibration
- `POST /api/measurement` - Add measurement
- `POST /api/calibration` - Run TPOINT calibration
- `GET /api/tpoint` - Get TPOINT parameters

### Autoguider
- `POST /api/guider/connect` - Connect autoguider
- `POST /api/guider/disconnect` - Disconnect autoguider
- `POST /api/guider/correction` - Send guider correction

### Advanced Features
- `GET /api/rotation` - Get rotation matrix
- `POST /api/pole` - Determine pole position
- `POST /api/trajectory/generate` - Generate trajectory
- `POST /api/trajectory/execute` - Execute trajectory
- `POST /api/encoders/:action` - Enable/disable encoders

## Configuration

### Environment Variables

The proxy server can be configured using environment variables:

```bash
# Port for the HTTP proxy server (default: 8080)
export PORT=8080

# gRPC server host and port (default: localhost:50051)
export GRPC_HOST=localhost
export GRPC_PORT=50051

# Log level (default: info)
export LOG_LEVEL=debug

# Allowed CORS origins (default: *)
export ALLOWED_ORIGINS=http://localhost:8080,http://192.168.1.100:8080
```

### Configuration File

For production deployment, create a `.env` file in the `web/proxy` directory:

```env
PORT=8080
GRPC_HOST=localhost
GRPC_PORT=50051
LOG_LEVEL=info
ALLOWED_ORIGINS=*
```

## Usage Guide

### Connecting to the Mount Controller

1. Open the web interface at `http://localhost:8080`
2. Click the "Connect" button in the top-right corner
3. Enter the gRPC server address (default: `localhost:50051`)
4. Click "Connect"

### Basic Operations

#### Slewing to Coordinates
1. Enter target RA (hours) and Dec (degrees) in the "Slewing Control" panel
2. Optionally enter an object name
3. Click "Slew to Coordinates"

#### Tracking an Object
1. Enter target coordinates
2. Click "Start Tracking"
3. The mount will track the object with sub-arcsecond accuracy

#### Parking the Mount
1. Click "Park" in the "Quick Actions" panel
2. The mount will move to the park position

#### Adding TPOINT Measurements
1. Point the telescope at a known star
2. Click "Add Measurement" in the TPOINT Calibration panel
3. Repeat for multiple stars across the sky
4. Click "Run Calibration" to compute TPOINT parameters

### Sky Map

The sky map shows:
- Current mount position (red crosshair)
- Target position (green circle, if set)
- Stars and constellations
- Coordinate grid (RA and Dec lines)

Controls:
- **Refresh**: Redraw the sky map
- **Auto-center**: Click mount position to center view
- **Zoom**: Use mouse wheel or buttons (future implementation)

## Security Considerations

### CORS Configuration
By default, the proxy server allows all origins (`*`). For production, restrict this to specific domains:

```bash
export ALLOWED_ORIGINS=https://observatory.example.com,https://admin.example.com
```

### HTTPS/SSL
For remote access, configure HTTPS:

```bash
# Generate SSL certificates
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365

# Start proxy with SSL
export SSL_KEY=key.pem
export SSL_CERT=cert.pem
npm start
```

### Authentication
For production deployment, add authentication middleware to the proxy server:

```javascript
// In server.js, add authentication middleware
app.use('/api/*', requireAuthMiddleware);
```

## Troubleshooting

### Common Issues

#### "Connection Failed" Error
1. Ensure the mount controller gRPC server is running:
   ```bash
   ./build/src/astro-mount-controller config/default.json
   ```
2. Check the gRPC server port (default: 50051)
3. Verify network connectivity

#### Proxy Server Won't Start
1. Check if port 8080 is already in use:
   ```bash
   netstat -tlnp | grep 8080
   ```
2. Change the port using the `PORT` environment variable
3. Check Node.js version: `node --version`

#### "gRPC Connection Failed" in Proxy Logs
1. Verify the gRPC server is running on the correct host/port
2. Check firewall settings
3. Ensure both services are on the same network

### Logs

- **Proxy Server Logs**: `web/proxy/proxy.log`
- **Browser Console**: Press F12 for developer tools
- **Mount Controller Logs**: Check the mount controller log file

## Performance

### Expected Performance
- **Response Time**: < 100ms for API calls
- **Update Rate**: 2 seconds for auto-refresh (configurable)
- **Memory Usage**: ~50 MB for proxy server
- **Concurrent Connections**: 10+ simultaneous clients

### Optimization Tips
1. **Reduce Auto-refresh Interval**: Increase the interval for slower networks
2. **Disable Sky Map**: The sky map uses CPU for rendering
3. **Limit Log Entries**: Reduce the number of retained log entries

## Browser Support

- **Chrome**: 60+
- **Firefox**: 55+
- **Safari**: 12+
- **Edge**: 79+
- **Mobile Safari**: 12+
- **Chrome for Android**: 60+

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make changes
4. Test thoroughly
5. Submit a pull request

### Development Setup

```bash
# Clone repository
git clone <repository-url>
cd projectx2/web

# Install dependencies
cd proxy
npm install

# Start development server
npm run dev

# Make changes to HTML/CSS/JS files
# The server will automatically restart
```

## License

This web interface is part of the Astronomical Mount Controller project and is available under the MIT License.

## Support

- **Documentation**: [docs/index.md](../doc-eng/index.md)
- **Issues**: [GitHub Issues](https://github.com/your-org/astro-mount-controller/issues)
- **Email**: support@astro-mount-controller.org