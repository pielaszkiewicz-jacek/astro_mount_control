// Astronomical Mount Controller Web Interface - API Client
// Communicates with gRPC server via HTTP/JSON proxy

class MountControllerAPI {
    constructor() {
        this.serverHost = 'localhost';
        this.serverPort = 50051;
        this.proxyPort = 8080;
        this.baseUrl = `http://${this.serverHost}:${this.proxyPort}/api`;
        this.connected = false;
        this.autoRefreshInterval = null;
        this.autoRefreshEnabled = false;
        this.refreshRate = 2000; // 2 seconds
    }

    // Connect to server via proxy
    async connect(host = this.serverHost, port = this.serverPort, proxyPort = this.proxyPort) {
        this.serverHost = host;
        this.serverPort = port;
        this.proxyPort = proxyPort;
        this.baseUrl = `http://${this.serverHost}:${this.proxyPort}/api`;

        try {
            const response = await fetch(`${this.baseUrl}/health`, {
                method: 'GET',
                headers: { 'Content-Type': 'application/json' }
            });

            if (response.ok) {
                const data = await response.json();
                this.connected = true;
                this.log('Connected to mount controller');
                return { success: true, data };
            } else {
                throw new Error(`Server responded with status: ${response.status}`);
            }
        } catch (error) {
            this.connected = false;
            this.log(`Connection failed: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Disconnect from server
    disconnect() {
        this.connected = false;
        this.stopAutoRefresh();
        this.log('Disconnected from mount controller');
    }

    // Get current mount state
    async getState() {
        try {
            const response = await fetch(`${this.baseUrl}/state`, {
                method: 'GET',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to get state: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Slew to coordinates
    async slewToCoordinates(ra, dec, objectName = '') {
        try {
            const response = await fetch(`${this.baseUrl}/slew`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    coordinates: {
                        ra: parseFloat(ra),
                        dec: parseFloat(dec),
                        objectName: objectName
                    }
                })
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log(`Slewing to RA ${ra}h, Dec ${dec}° (${objectName || 'unnamed object'})`, 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to slew: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Track object
    async trackObject(ra, dec) {
        try {
            const response = await fetch(`${this.baseUrl}/track`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    coordinates: {
                        ra: parseFloat(ra),
                        dec: parseFloat(dec)
                    }
                })
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log(`Started tracking RA ${ra}h, Dec ${dec}°`, 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to track: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Stop mount
    async stop() {
        try {
            const response = await fetch(`${this.baseUrl}/stop`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Mount stopped', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to stop: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Park mount
    async park() {
        try {
            const response = await fetch(`${this.baseUrl}/park`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Mount parked', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to park: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Unpark mount
    async unpark() {
        try {
            const response = await fetch(`${this.baseUrl}/unpark`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Mount unparked', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to unpark: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Go to home position
    async goHome() {
        try {
            const response = await fetch(`${this.baseUrl}/home`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Moving to home position', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to go home: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Get configuration
    async getConfiguration() {
        try {
            const response = await fetch(`${this.baseUrl}/configuration`, {
                method: 'GET',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to get configuration: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Update configuration
    async updateConfiguration(config) {
        try {
            const response = await fetch(`${this.baseUrl}/configuration`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Configuration updated', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to update configuration: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Add TPOINT measurement
    async addMeasurement() {
        try {
            const response = await fetch(`${this.baseUrl}/measurement`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Measurement added', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to add measurement: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Run TPOINT calibration
    async runCalibration() {
        try {
            const response = await fetch(`${this.baseUrl}/calibration`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('TPOINT calibration started', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to run calibration: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Get TPOINT parameters
    async getTPointParameters() {
        try {
            const response = await fetch(`${this.baseUrl}/tpoint`, {
                method: 'GET',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to get TPOINT parameters: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Connect guider
    async connectGuider(connectionString = 'tcp://localhost:7624', maxCorrection = 10.0, aggression = 0.5) {
        try {
            const response = await fetch(`${this.baseUrl}/guider/connect`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    connectionString,
                    maxCorrection,
                    aggression
                })
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Guider connected', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to connect guider: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Disconnect guider
    async disconnectGuider() {
        try {
            const response = await fetch(`${this.baseUrl}/guider/disconnect`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log('Guider disconnected', 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to disconnect guider: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Send guider correction
    async sendGuiderCorrection(raCorrection, decCorrection) {
        try {
            const response = await fetch(`${this.baseUrl}/guider/correction`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    raCorrection: parseFloat(raCorrection),
                    decCorrection: parseFloat(decCorrection)
                })
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            this.log(`Guider correction sent: RA ${raCorrection}", Dec ${decCorrection}"`, 'success');
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to send guider correction: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Check system health
    async checkHealth() {
        try {
            const response = await fetch(`${this.baseUrl}/health`, {
                method: 'GET',
                headers: { 'Content-Type': 'application/json' }
            });

            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const data = await response.json();
            return { success: true, data };
        } catch (error) {
            this.log(`Failed to check health: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }

    // Start auto-refresh
    startAutoRefresh(callback, interval = this.refreshRate) {
        if (this.autoRefreshInterval) {
            clearInterval(this.autoRefreshInterval);
        }
        
        this.autoRefreshEnabled = true;
        this.autoRefreshInterval = setInterval(async () => {
            if (this.connected) {
                const state = await this.getState();
                if (state.success) {
                    callback(state.data);
                }
            }
        }, interval);
        
        this.log(`Auto-refresh started (${interval}ms interval)`);
    }

    // Stop auto-refresh
    stopAutoRefresh() {
        if (this.autoRefreshInterval) {
            clearInterval(this.autoRefreshInterval);
            this.autoRefreshInterval = null;
            this.autoRefreshEnabled = false;
            this.log('Auto-refresh stopped');
        }
    }

    // Toggle auto-refresh
    toggleAutoRefresh(callback) {
        if (this.autoRefreshEnabled) {
            this.stopAutoRefresh();
            return false;
        } else {
            this.startAutoRefresh(callback);
            return true;
        }
    }

    // Log utility
    log(message, type = 'info') {
        const timestamp = new Date().toISOString().split('T')[1].split('.')[0];
        const logEntry = {
            time: timestamp,
            message: message,
            type: type
        };
        
        // Emit log event
        if (typeof window !== 'undefined') {
            const event = new CustomEvent('mount-log', { detail: logEntry });
            window.dispatchEvent(event);
        }
        
        // Console log
        const colors = {
            info: '\x1b[36m',    // cyan
            success: '\x1b[32m',  // green
            warning: '\x1b[33m',  // yellow
            error: '\x1b[31m'     // red
        };
        
        console.log(`${colors[type] || ''}[${timestamp}] ${message}\x1b[0m`);
    }

    // Format RA (hours to HH:MM:SS)
    formatRA(hours) {
        const totalSeconds = hours * 3600;
        const h = Math.floor(totalSeconds / 3600);
        const m = Math.floor((totalSeconds % 3600) / 60);
        const s = totalSeconds % 60;
        return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toFixed(2).padStart(5, '0')}`;
    }

    // Format Dec (degrees to ±DD:MM:SS)
    formatDec(degrees) {
        const sign = degrees >= 0 ? '+' : '-';
        const absDeg = Math.abs(degrees);
        const d = Math.floor(absDeg);
        const m = Math.floor((absDeg - d) * 60);
        const s = (absDeg - d - m/60) * 3600;
        return `${sign}${d.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toFixed(2).padStart(5, '0')}`;
    }

    // Convert RA from HH:MM:SS to hours
    parseRA(raString) {
        const parts = raString.split(':').map(parseFloat);
        if (parts.length === 3) {
            return parts[0] + parts[1]/60 + parts[2]/3600;
        }
        return parseFloat(raString) || 0;
    }

    // Convert Dec from ±DD:MM:SS to degrees
    parseDec(decString) {
        const sign = decString.charAt(0) === '-' ? -1 : 1;
        const absString = decString.replace(/^[+-]/, '');
        const parts = absString.split(':').map(parseFloat);
        if (parts.length === 3) {
            return sign * (parts[0] + parts[1]/60 + parts[2]/3600);
        }
        return parseFloat(decString) || 0;
    }

    // Calculate angular distance between two coordinates (degrees)
    angularDistance(ra1, dec1, ra2, dec2) {
        const ra1Rad = ra1 * Math.PI / 12; // hours to radians
        const dec1Rad = dec1 * Math.PI / 180;
        const ra2Rad = ra2 * Math.PI / 12;
        const dec2Rad = dec2 * Math.PI / 180;
        
        return Math.acos(
            Math.sin(dec1Rad) * Math.sin(dec2Rad) +
            Math.cos(dec1Rad) * Math.cos(dec2Rad) * Math.cos(ra1Rad - ra2Rad)
        ) * 180 / Math.PI;
    }

    // Calculate slew time between coordinates (rough estimate)
    estimateSlewTime(ra1, dec1, ra2, dec2, maxSlewRate = 5.0) {
        const distance = this.angularDistance(ra1, dec1, ra2, dec2);
        const time = distance / maxSlewRate; // seconds
        return {
            seconds: time,
            minutes: time / 60,
            hours: time / 3600
        };
    }
}

// Create global instance
if (typeof window !== 'undefined') {
    window.mountControllerAPI = new MountControllerAPI();
}