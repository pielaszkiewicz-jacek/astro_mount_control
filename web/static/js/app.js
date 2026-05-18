// Astronomical Mount Controller Web Interface - Main Application

class MountControllerApp {
    constructor() {
        this.api = window.mountControllerAPI;
        this.connected = false;
        this.currentState = null;
        this.updateInterval = null;
        this.autoRefreshEnabled = false;
        this.lastUpdate = null;
        
        // Initialize Toastr
        this.initToastr();
        
        // Initialize event listeners
        this.initEventListeners();
        
        // Initialize sky map
        if (window.SkyMap) {
            this.skyMap = new window.SkyMap('sky-canvas');
        }
        
        // Initialize object database manager
        if (window.ObjectDatabaseManager) {
            this.objectDB = new window.ObjectDatabaseManager(this);
        }
        
        // Log application start
        this.log('Web interface initialized', 'info');
        
        // Attempt auto-connect if server is running
        this.autoConnect();
    }
    
    // Initialize Toastr notifications
    initToastr() {
        toastr.options = {
            closeButton: true,
            debug: false,
            newestOnTop: true,
            progressBar: true,
            positionClass: "toast-top-right",
            preventDuplicates: false,
            onclick: null,
            showDuration: 300,
            hideDuration: 1000,
            timeOut: 5000,
            extendedTimeOut: 1000,
            showEasing: "swing",
            hideEasing: "linear",
            showMethod: "fadeIn",
            hideMethod: "fadeOut"
        };
    }
    
    // Initialize all event listeners
    initEventListeners() {
        // Connection
        document.getElementById('connect-btn').addEventListener('click', () => this.showConnectionModal());
        document.getElementById('modal-connect-btn').addEventListener('click', () => this.connect());
        document.querySelectorAll('.modal-close').forEach(btn => {
            btn.addEventListener('click', () => this.hideConnectionModal());
        });
        
        // Quick actions
        document.getElementById('stop-btn').addEventListener('click', () => this.stop());
        document.getElementById('park-btn').addEventListener('click', () => this.park());
        document.getElementById('unpark-btn').addEventListener('click', () => this.unpark());
        document.getElementById('home-btn').addEventListener('click', () => this.goHome());
        
        // Slewing controls
        document.getElementById('slew-btn').addEventListener('click', () => this.slewToCoordinates());
        document.getElementById('track-btn').addEventListener('click', () => this.trackObject());
        document.getElementById('current-ra-btn').addEventListener('click', () => this.useCurrentRA());
        document.getElementById('current-dec-btn').addEventListener('click', () => this.useCurrentDec());
        
        // TPOINT calibration
        document.getElementById('add-measurement-btn').addEventListener('click', () => this.addMeasurement());
        document.getElementById('run-calibration-btn').addEventListener('click', () => this.runCalibration());
        document.getElementById('get-tpoint-btn').addEventListener('click', () => this.getTPointParameters());
        
        // Guider controls
        document.getElementById('connect-guider-btn').addEventListener('click', () => this.connectGuider());
        document.getElementById('disconnect-guider-btn').addEventListener('click', () => this.disconnectGuider());
        document.getElementById('send-guider-correction-btn').addEventListener('click', () => this.sendGuiderCorrection());
        
        // Configuration
        document.getElementById('load-config-btn').addEventListener('click', () => this.loadConfiguration());
        document.getElementById('save-config-btn').addEventListener('click', () => this.saveConfiguration());
        
        // System health
        document.getElementById('health-check-btn').addEventListener('click', () => this.checkHealth());
        
        // Logs
        document.getElementById('clear-logs-btn').addEventListener('click', () => this.clearLogs());
        
        // Sky map
        document.getElementById('refresh-map').addEventListener('click', () => this.refreshSkyMap());
        
        // Footer controls
        document.getElementById('auto-refresh-toggle').addEventListener('click', () => this.toggleAutoRefresh());
        document.getElementById('fullscreen-btn').addEventListener('click', () => this.toggleFullscreen());
        
        // Listen for log events from API
        window.addEventListener('mount-log', (event) => {
            this.addLogEntry(event.detail);
        });
    }
    
    // Show connection modal
    showConnectionModal() {
        document.getElementById('connection-modal').classList.add('active');
    }
    
    // Hide connection modal
    hideConnectionModal() {
        document.getElementById('connection-modal').classList.remove('active');
    }
    
    // Connect to mount controller
    async connect() {
        const host = document.getElementById('server-host').value;
        const port = parseInt(document.getElementById('server-port').value);
        const proxyPort = 8080; // Default proxy port
        
        this.showLoading('Connecting to mount controller...');
        
        try {
            const result = await this.api.connect(host, port, proxyPort);
            
            if (result.success) {
                this.connected = true;
                this.updateConnectionStatus(true);
                this.hideConnectionModal();
                this.startAutoRefresh();
                
                toastr.success('Connected to mount controller');
                this.log('Connection established', 'success');
                
                // Load initial state
                await this.updateState();
            } else {
                throw new Error(result.error);
            }
        } catch (error) {
            toastr.error(`Connection failed: ${error.message}`);
            this.log(`Connection failed: ${error.message}`, 'error');
        } finally {
            this.hideLoading();
        }
    }
    
    // Auto-connect if server is running locally
    async autoConnect() {
        this.showLoading('Attempting auto-connection...');
        
        try {
            const result = await this.api.connect();
            
            if (result.success) {
                this.connected = true;
                this.updateConnectionStatus(true);
                this.startAutoRefresh();
                
                toastr.success('Auto-connected to mount controller');
                this.log('Auto-connection successful', 'success');
                
                await this.updateState();
            }
        } catch (error) {
            // Auto-connect failed, this is expected if server is not running
            this.log('Auto-connect failed (server may not be running)', 'warning');
        } finally {
            this.hideLoading();
        }
    }
    
    // Update connection status display
    updateConnectionStatus(connected) {
        const statusElement = document.getElementById('connection-status');
        const connectButton = document.getElementById('connect-btn');
        
        if (connected) {
            statusElement.className = 'status-connected';
            statusElement.innerHTML = '<i class="fas fa-plug"></i> Connected';
            connectButton.innerHTML = '<i class="fas fa-unlink"></i> Disconnect';
            connectButton.removeEventListener('click', () => this.showConnectionModal());
            connectButton.addEventListener('click', () => this.disconnect());
        } else {
            statusElement.className = 'status-disconnected';
            statusElement.innerHTML = '<i class="fas fa-plug"></i> Disconnected';
            connectButton.innerHTML = '<i class="fas fa-link"></i> Connect';
            connectButton.removeEventListener('click', () => this.disconnect());
            connectButton.addEventListener('click', () => this.showConnectionModal());
        }
    }
    
    // Disconnect from mount controller
    disconnect() {
        this.api.disconnect();
        this.connected = false;
        this.updateConnectionStatus(false);
        this.stopAutoRefresh();
        
        toastr.info('Disconnected from mount controller');
        this.log('Disconnected', 'info');
        
        // Reset display
        this.resetDisplay();
    }
    
    // Start auto-refresh
    startAutoRefresh() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
        }
        
        this.autoRefreshEnabled = true;
        this.updateInterval = setInterval(async () => {
            if (this.connected) {
                await this.updateState();
            }
        }, 2000); // Update every 2 seconds
        
        document.getElementById('auto-refresh-toggle').innerHTML = 
            '<i class="fas fa-sync"></i> Auto-refresh: ON';
        
        this.log('Auto-refresh started', 'info');
    }
    
    // Stop auto-refresh
    stopAutoRefresh() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
        
        this.autoRefreshEnabled = false;
        document.getElementById('auto-refresh-toggle').innerHTML = 
            '<i class="fas fa-sync"></i> Auto-refresh: OFF';
        
        this.log('Auto-refresh stopped', 'info');
    }
    
    // Toggle auto-refresh
    toggleAutoRefresh() {
        if (this.autoRefreshEnabled) {
            this.stopAutoRefresh();
        } else {
            this.startAutoRefresh();
        }
    }
    
    // Update mount state from server
    async updateState() {
        if (!this.connected) return;
        
        try {
            const result = await this.api.getState();
            
            if (result.success) {
                this.currentState = result.data;
                this.updateDisplay();
                this.lastUpdate = new Date();
                this.updateLastUpdateTime();
            }
        } catch (error) {
            this.log(`Failed to update state: ${error.message}`, 'error');
        }
    }
    
    // Update all display elements with current state
    updateDisplay() {
        if (!this.currentState) return;
        
        const state = this.currentState;
        
        // Mount status
        document.getElementById('mount-state').textContent = this.formatMountStatus(state.status);
        document.getElementById('ra-position').textContent = this.formatRA(state.tracked_object?.coordinates?.ra || 0);
        document.getElementById('dec-position').textContent = this.formatDec(state.tracked_object?.coordinates?.dec || 0);
        document.getElementById('tracking-error').textContent = `${(state.pointing_error || 0).toFixed(2)}"`;
        document.getElementById('temperature').textContent = `${(state.temperature || 20.0).toFixed(1)}°C`;
        document.getElementById('pier-side').textContent = state.pier_side > 0 ? 'EAST' : 'WEST';
        
        // TPOINT status
        const tpoint = state.tpoint_params;
        document.getElementById('tpoint-status').textContent = tpoint?.coefficients?.length > 0 ? 'Calibrated' : 'Not Calibrated';
        document.getElementById('chi-squared').textContent = (tpoint?.chi_squared || 0).toFixed(3);
        document.getElementById('measurement-count').textContent = tpoint?.coefficients?.length || 0;
        
        // Guider status
        document.getElementById('guider-status').textContent = state.guider_active ? 'Connected' : 'Disconnected';
        document.getElementById('guider-correction-ra').textContent = '0.00"';
        document.getElementById('guider-correction-dec').textContent = '0.00"';
        
        // System health
        const cpuUsage = state.metrics?.cpu_usage_percent || 15;
        const memoryUsage = state.metrics?.memory_usage_mb || 200;
        const totalMemory = 4096; // Assume 4GB for now
        
        document.getElementById('cpu-usage-bar').style.width = `${cpuUsage}%`;
        document.getElementById('cpu-usage-text').textContent = `${cpuUsage.toFixed(1)}%`;
        document.getElementById('memory-usage-bar').style.width = `${(memoryUsage / totalMemory * 100).toFixed(1)}%`;
        document.getElementById('memory-usage-text').textContent = `${memoryUsage.toFixed(0)} MB`;
        
        // Update sky map if available
        if (this.skyMap && state.tracked_object?.coordinates) {
            const coords = state.tracked_object.coordinates;
            this.skyMap.updateMountPosition(coords.ra, coords.dec);
            document.getElementById('map-mount-position').textContent = 
                `(${coords.ra.toFixed(2)}h, ${coords.dec.toFixed(2)}°)`;
        }
        
        // Update server address in footer
        document.getElementById('server-address').textContent = 
            `${this.api.serverHost}:${this.api.serverPort}`;
    }
    
    // Reset display when disconnected
    resetDisplay() {
        document.getElementById('mount-state').textContent = 'DISCONNECTED';
        document.getElementById('ra-position').textContent = '0.000°';
        document.getElementById('dec-position').textContent = '0.000°';
        document.getElementById('tracking-error').textContent = '0.00"';
        document.getElementById('temperature').textContent = '20.0°C';
        document.getElementById('pier-side').textContent = 'EAST';
        
        document.getElementById('tpoint-status').textContent = 'Not Calibrated';
        document.getElementById('chi-squared').textContent = '0.000';
        document.getElementById('measurement-count').textContent = '0';
        
        document.getElementById('guider-status').textContent = 'Disconnected';
        document.getElementById('guider-correction-ra').textContent = '0.00"';
        document.getElementById('guider-correction-dec').textContent = '0.00"';
        
        document.getElementById('cpu-usage-bar').style.width = '15%';
        document.getElementById('cpu-usage-text').textContent = '15%';
        document.getElementById('memory-usage-bar').style.width = '45%';
        document.getElementById('memory-usage-text').textContent = '45%';
    }
    
    // Update last update time in footer
    updateLastUpdateTime() {
        if (this.lastUpdate) {
            const timeStr = this.lastUpdate.toLocaleTimeString();
            document.getElementById('last-update').textContent = timeStr;
        }
    }
    
    // Slew to coordinates
    async slewToCoordinates() {
        const ra = document.getElementById('target-ra').value;
        const dec = document.getElementById('target-dec').value;
        const objectName = document.getElementById('object-name').value;
        
        if (!ra || !dec) {
            toastr.warning('Please enter both RA and Dec coordinates');
            return;
        }
        
        try {
            const result = await this.api.slewToCoordinates(ra, dec, objectName);
            
            if (result.success) {
                toastr.success(`Slewing to ${objectName || 'target'} (RA: ${ra}h, Dec: ${dec}°)`);
                
                // Update current coordinates inputs
                if (objectName) {
                    this.log(`Started slewing to ${objectName}`, 'success');
                }
            }
        } catch (error) {
            toastr.error(`Slew failed: ${error.message}`);
        }
    }
    
    // Track object
    async trackObject() {
        const ra = document.getElementById('target-ra').value;
        const dec = document.getElementById('target-dec').value;
        
        if (!ra || !dec) {
            toastr.warning('Please enter both RA and Dec coordinates');
            return;
        }
        
        try {
            const result = await this.api.trackObject(ra, dec);
            
            if (result.success) {
                toastr.success(`Tracking started (RA: ${ra}h, Dec: ${dec}°)`);
                this.log(`Started tracking RA ${ra}h, Dec ${dec}°`, 'success');
            }
        } catch (error) {
            toastr.error(`Track failed: ${error.message}`);
        }
    }
    
    // Use current RA for slewing
    useCurrentRA() {
        if (this.currentState?.tracked_object?.coordinates?.ra) {
            const ra = this.currentState.tracked_object.coordinates.ra;
            document.getElementById('target-ra').value = ra.toFixed(3);
            toastr.info(`Set RA to current value: ${ra.toFixed(3)}h`);
        } else {
            toastr.warning('Current RA not available');
        }
    }
    
    // Use current Dec for slewing
    useCurrentDec() {
        if (this.currentState?.tracked_object?.coordinates?.dec) {
            const dec = this.currentState.tracked_object.coordinates.dec;
            document.getElementById('target-dec').value = dec.toFixed(3);
            toastr.info(`Set Dec to current value: ${dec.toFixed(3)}°`);
        } else {
            toastr.warning('Current Dec not available');
        }
    }
    
    // Stop mount
    async stop() {
        try {
            const result = await this.api.stop();
            
            if (result.success) {
                toastr.success('Mount stopped');
                this.log('Mount stopped by user', 'warning');
            }
        } catch (error) {
            toastr.error(`Stop failed: ${error.message}`);
        }
    }
    
    // Park mount
    async park() {
        try {
            const result = await this.api.park();
            
            if (result.success) {
                toastr.success('Mount parked');
                this.log('Mount parked', 'success');
            }
        } catch (error) {
            toastr.error(`Park failed: ${error.message}`);
        }
    }
    
    // Unpark mount
    async unpark() {
        try {
            const result = await this.api.unpark();
            
            if (result.success) {
                toastr.success('Mount unparked');
                this.log('Mount unparked', 'success');
            }
        } catch (error) {
            toastr.error(`Unpark failed: ${error.message}`);
        }
    }
    
    // Go to home position
    async goHome() {
        try {
            const result = await this.api.goHome();
            
            if (result.success) {
                toastr.success('Moving to home position');
                this.log('Moving to home position', 'info');
            }
        } catch (error) {
            toastr.error(`Go home failed: ${error.message}`);
        }
    }
    
    // Add TPOINT measurement
    async addMeasurement() {
        try {
            const result = await this.api.addMeasurement();
            
            if (result.success) {
                toastr.success('Measurement added');
                this.log('TPOINT measurement added', 'success');
            }
        } catch (error) {
            toastr.error(`Add measurement failed: ${error.message}`);
        }
    }
    
    // Run TPOINT calibration
    async runCalibration() {
        try {
            const result = await this.api.runCalibration();
            
            if (result.success) {
                toastr.success('TPOINT calibration started');
                this.log('TPOINT calibration started', 'info');
            }
        } catch (error) {
            toastr.error(`Calibration failed: ${error.message}`);
        }
    }
    
    // Get TPOINT parameters
    async getTPointParameters() {
        try {
            const result = await this.api.getTPointParameters();
            
            if (result.success) {
                toastr.success('TPOINT parameters retrieved');
                this.log('TPOINT parameters retrieved', 'info');
                
                // Display parameters (simplified for now)
                console.log('TPOINT parameters:', result.data);
            }
        } catch (error) {
            toastr.error(`Get TPOINT parameters failed: ${error.message}`);
        }
    }
    
    // Connect guider
    async connectGuider() {
        try {
            const result = await this.api.connectGuider();
            
            if (result.success) {
                toastr.success('Guider connected');
                this.log('Autoguider connected', 'success');
            }
        } catch (error) {
            toastr.error(`Connect guider failed: ${error.message}`);
        }
    }
    
    // Disconnect guider
    async disconnectGuider() {
        try {
            const result = await this.api.disconnectGuider();
            
            if (result.success) {
                toastr.success('Guider disconnected');
                this.log('Autoguider disconnected', 'info');
            }
        } catch (error) {
            toastr.error(`Disconnect guider failed: ${error.message}`);
        }
    }
    
    // Send guider correction
    async sendGuiderCorrection() {
        // For now, send zero correction as example
        try {
            const result = await this.api.sendGuiderCorrection(0, 0);
            
            if (result.success) {
                toastr.success('Guider correction sent');
                this.log('Zero correction sent to autoguider', 'info');
            }
        } catch (error) {
            toastr.error(`Send guider correction failed: ${error.message}`);
        }
    }
    
    // Load configuration
    async loadConfiguration() {
        try {
            const result = await this.api.getConfiguration();
            
            if (result.success) {
                const config = result.data;
                
                // Update form fields (simplified for now)
                document.getElementById('config-latitude').value = config.latitude || 52.2297;
                document.getElementById('config-longitude').value = config.longitude || 21.0122;
                document.getElementById('config-altitude').value = config.altitude || 100.0;
                document.getElementById('config-max-slew-rate').value = config.max_slew_rate || 5.0;
                document.getElementById('config-max-tracking-rate').value = config.max_tracking_rate || 0.004178;
                
                toastr.success('Configuration loaded');
                this.log('Configuration loaded from server', 'success');
            }
        } catch (error) {
            toastr.error(`Load configuration failed: ${error.message}`);
        }
    }
    
    // Save configuration
    async saveConfiguration() {
        const config = {
            latitude: parseFloat(document.getElementById('config-latitude').value),
            longitude: parseFloat(document.getElementById('config-longitude').value),
            altitude: parseFloat(document.getElementById('config-altitude').value),
            max_slew_rate: parseFloat(document.getElementById('config-max-slew-rate').value),
            max_tracking_rate: parseFloat(document.getElementById('config-max-tracking-rate').value)
        };
        
        try {
            const result = await this.api.updateConfiguration(config);
            
            if (result.success) {
                toastr.success('Configuration saved');
                this.log('Configuration saved to server', 'success');
            }
        } catch (error) {
            toastr.error(`Save configuration failed: ${error.message}`);
        }
    }
    
    // Check system health
    async checkHealth() {
        try {
            const result = await this.api.checkHealth();
            
            if (result.success) {
                toastr.success('System health check passed');
                this.log('System health check completed', 'info');
                
                // Update health display
                if (result.data.metrics) {
                    const metrics = result.data.metrics;
                    const cpuUsage = metrics.cpu_usage_percent || 15;
                    const memoryUsage = metrics.memory_usage_mb || 200;
                    const totalMemory = 4096;
                    
                    document.getElementById('cpu-usage-bar').style.width = `${cpuUsage}%`;
                    document.getElementById('cpu-usage-text').textContent = `${cpuUsage.toFixed(1)}%`;
                    document.getElementById('memory-usage-bar').style.width = `${(memoryUsage / totalMemory * 100).toFixed(1)}%`;
                    document.getElementById('memory-usage-text').textContent = `${memoryUsage.toFixed(0)} MB`;
                }
            }
        } catch (error) {
            toastr.error(`Health check failed: ${error.message}`);
        }
    }
    
    // Refresh sky map
    refreshSkyMap() {
        if (this.skyMap) {
            this.skyMap.refresh();
            toastr.info('Sky map refreshed');
            this.log('Sky map refreshed', 'info');
        }
    }
    
    // Toggle fullscreen
    toggleFullscreen() {
        if (!document.fullscreenElement) {
            document.documentElement.requestFullscreen().catch(err => {
                toastr.error(`Error attempting to enable fullscreen: ${err.message}`);
            });
            document.getElementById('fullscreen-btn').innerHTML = '<i class="fas fa-compress"></i> Exit Fullscreen';
        } else {
            if (document.exitFullscreen) {
                document.exitFullscreen();
                document.getElementById('fullscreen-btn').innerHTML = '<i class="fas fa-expand"></i> Fullscreen';
            }
        }
    }
    
    // Add log entry to log container
    addLogEntry(logEntry) {
        const container = document.getElementById('log-container');
        const entry = document.createElement('div');
        
        entry.className = `log-entry log-${logEntry.type}`;
        entry.innerHTML = `
            <span class="log-time">${logEntry.time}</span>
            <span class="log-message">${logEntry.message}</span>
        `;
        
        container.appendChild(entry);
        container.scrollTop = container.scrollHeight;
        
        // Limit log entries to 100
        while (container.children.length > 100) {
            container.removeChild(container.firstChild);
        }
    }
    
    // Clear logs
    clearLogs() {
        const container = document.getElementById('log-container');
        container.innerHTML = '';
        this.log('Logs cleared', 'info');
        toastr.info('Logs cleared');
    }
    
    // Format mount status
    formatMountStatus(status) {
        const statusMap = {
            0: 'UNKNOWN',
            1: 'IDLE',
            2: 'SLEWING',
            3: 'TRACKING',
            4: 'PARKED',
            5: 'ERROR'
        };
        return statusMap[status] || 'UNKNOWN';
    }
    
    // Format RA for display
    formatRA(hours) {
        return this.api.formatRA(hours);
    }
    
    // Format Dec for display
    formatDec(degrees) {
        return this.api.formatDec(degrees);
    }
    
    // Show loading indicator
    showLoading(message = 'Loading...') {
        // Create loading overlay if it doesn't exist
        let overlay = document.getElementById('loading-overlay');
        if (!overlay) {
            overlay = document.createElement('div');
            overlay.id = 'loading-overlay';
            overlay.style.cssText = `
                position: fixed;
                top: 0;
                left: 0;
                width: 100%;
                height: 100%;
                background: rgba(0, 0, 0, 0.7);
                display: flex;
                flex-direction: column;
                justify-content: center;
                align-items: center;
                z-index: 9999;
                color: white;
                font-size: 1.2rem;
            `;
            
            const spinner = document.createElement('div');
            spinner.style.cssText = `
                border: 8px solid #f3f3f3;
                border-top: 8px solid #3498db;
                border-radius: 50%;
                width: 60px;
                height: 60px;
                animation: spin 1s linear infinite;
                margin-bottom: 20px;
            `;
            
            const text = document.createElement('div');
            text.id = 'loading-text';
            text.textContent = message;
            
            overlay.appendChild(spinner);
            overlay.appendChild(text);
            
            // Add CSS animation
            const style = document.createElement('style');
            style.textContent = `
                @keyframes spin {
                    0% { transform: rotate(0deg); }
                    100% { transform: rotate(360deg); }
                }
            `;
            document.head.appendChild(style);
            
            document.body.appendChild(overlay);
        } else {
            overlay.style.display = 'flex';
            document.getElementById('loading-text').textContent = message;
        }
    }
    
    // Hide loading indicator
    hideLoading() {
        const overlay = document.getElementById('loading-overlay');
        if (overlay) {
            overlay.style.display = 'none';
        }
    }
    
    // Log utility
    log(message, type = 'info') {
        const timestamp = new Date().toISOString().split('T')[1].split('.')[0];
        this.addLogEntry({ time: timestamp, message, type });
    }
}

// Initialize application when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
    window.mountControllerApp = new MountControllerApp();
});