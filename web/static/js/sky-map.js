// Astronomical Mount Controller - Sky Map Visualization

class SkyMap {
    constructor(canvasId) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
        this.width = this.canvas.width;
        this.height = this.canvas.height;
        
        // Configuration
        this.config = {
            backgroundColor: '#0f3460',
            gridColor: 'rgba(255, 255, 255, 0.2)',
            gridLineWidth: 1,
            starColor: 'rgba(255, 255, 255, 0.8)',
            starSize: 2,
            mountColor: '#e74c3c',
            mountSize: 8,
            targetColor: '#2ecc71',
            targetSize: 6,
            textColor: '#ecf0f1',
            fontSize: 12,
            fovWidth: 60, // degrees
            fovHeight: 40, // degrees
            centerRA: 12, // hours
            centerDec: 0, // degrees
            showGrid: true,
            showStars: true,
            showConstellations: true
        };
        
        // Mount position
        this.mountPosition = { ra: 0, dec: 0 };
        this.targetPosition = null;
        
        // Star catalog (simplified for demo)
        this.stars = this.generateStarCatalog();
        this.constellations = this.generateConstellationLines();
        
        // Initialize
        this.draw();
    }
    
    // Generate a simple star catalog for demo purposes
    generateStarCatalog() {
        const stars = [];
        const starCount = 100;
        
        // Generate random stars within the view
        for (let i = 0; i < starCount; i++) {
            const ra = this.config.centerRA + (Math.random() - 0.5) * this.config.fovWidth / 15; // Convert degrees to hours
            const dec = this.config.centerDec + (Math.random() - 0.5) * this.config.fovHeight;
            const magnitude = 1 + Math.random() * 5; // Brighter stars have lower magnitude
            
            // Only add stars within reasonable bounds
            if (dec >= -90 && dec <= 90) {
                stars.push({
                    ra: this.normalizeRA(ra),
                    dec: dec,
                    magnitude: magnitude,
                    name: i < 10 ? `Star ${i+1}` : null // Name only first 10 stars
                });
            }
        }
        
        // Add some well-known stars
        stars.push({ ra: 18.6156, dec: 38.7836, magnitude: 0.03, name: 'Vega' });
        stars.push({ ra: 16.4901, dec: -26.432, magnitude: -1.46, name: 'Antares' });
        stars.push({ ra: 5.9195, dec: 7.407, magnitude: 0.45, name: 'Betelgeuse' });
        stars.push({ ra: 6.7525, dec: -16.716, magnitude: -1.44, name: 'Sirius' });
        stars.push({ ra: 14.6608, dec: -60.835, magnitude: 0.61, name: 'Hadar' });
        stars.push({ ra: 20.6905, dec: 45.28, magnitude: 1.25, name: 'Deneb' });
        
        return stars;
    }
    
    // Generate constellation lines (simplified)
    generateConstellationLines() {
        return [
            // Orion
            { from: { ra: 5.9195, dec: 7.407 }, to: { ra: 5.795, dec: -1.943 }, color: 'rgba(52, 152, 219, 0.6)' },
            { from: { ra: 5.795, dec: -1.943 }, to: { ra: 5.678, dec: -9.67 }, color: 'rgba(52, 152, 219, 0.6)' },
            { from: { ra: 5.9195, dec: 7.407 }, to: { ra: 5.418, dec: 6.35 }, color: 'rgba(52, 152, 219, 0.6)' },
            
            // Ursa Major (Big Dipper)
            { from: { ra: 11.062, dec: 61.751 }, to: { ra: 11.767, dec: 53.695 }, color: 'rgba(46, 204, 113, 0.6)' },
            { from: { ra: 11.767, dec: 53.695 }, to: { ra: 12.9, dec: 55.96 }, color: 'rgba(46, 204, 113, 0.6)' },
            { from: { ra: 12.9, dec: 55.96 }, to: { ra: 13.792, dec: 49.313 }, color: 'rgba(46, 204, 113, 0.6)' },
            
            // Cassiopeia
            { from: { ra: 0.675, dec: 56.537 }, to: { ra: 1.431, dec: 60.235 }, color: 'rgba(155, 89, 182, 0.6)' },
            { from: { ra: 1.431, dec: 60.235 }, to: { ra: 2.294, dec: 59.15 }, color: 'rgba(155, 89, 182, 0.6)' }
        ];
    }
    
    // Normalize RA to 0-24 hours
    normalizeRA(ra) {
        while (ra < 0) ra += 24;
        while (ra >= 24) ra -= 24;
        return ra;
    }
    
    // Convert RA/Dec to canvas coordinates
    raDecToCanvas(ra, dec) {
        // Convert RA from hours to degrees
        const raDeg = ra * 15; // 1 hour = 15 degrees
        
        // Calculate relative positions within FOV
        const raOffset = (raDeg - this.config.centerRA * 15) / this.config.fovWidth;
        const decOffset = (dec - this.config.centerDec) / this.config.fovHeight;
        
        // Convert to canvas coordinates (center is at canvas center)
        const x = this.width / 2 + raOffset * this.width;
        const y = this.height / 2 - decOffset * this.height;
        
        return { x, y };
    }
    
    // Check if coordinates are within view
    isInView(ra, dec) {
        const raDeg = ra * 15;
        const centerRaDeg = this.config.centerRA * 15;
        
        const raDiff = Math.abs(raDeg - centerRaDeg);
        const decDiff = Math.abs(dec - this.config.centerDec);
        
        // Handle RA wrap-around
        const raDistance = Math.min(raDiff, 360 - raDiff);
        
        return raDistance <= this.config.fovWidth / 2 && decDiff <= this.config.fovHeight / 2;
    }
    
    // Draw the sky map
    draw() {
        // Clear canvas
        this.ctx.fillStyle = this.config.backgroundColor;
        this.ctx.fillRect(0, 0, this.width, this.height);
        
        // Draw coordinate grid
        if (this.config.showGrid) {
            this.drawGrid();
        }
        
        // Draw constellation lines
        if (this.config.showConstellations) {
            this.drawConstellations();
        }
        
        // Draw stars
        if (this.config.showStars) {
            this.drawStars();
        }
        
        // Draw mount position
        this.drawMountPosition();
        
        // Draw target position if set
        if (this.targetPosition) {
            this.drawTargetPosition();
        }
        
        // Draw coordinate labels
        this.drawLabels();
    }
    
    // Draw coordinate grid
    drawGrid() {
        this.ctx.strokeStyle = this.config.gridColor;
        this.ctx.lineWidth = this.config.gridLineWidth;
        
        // RA lines (vertical)
        const raStep = 5; // degrees
        for (let raDeg = 0; raDeg < 360; raDeg += raStep) {
            const ra = raDeg / 15;
            if (this.isInView(ra, this.config.centerDec)) {
                const pos = this.raDecToCanvas(ra, this.config.centerDec);
                this.ctx.beginPath();
                this.ctx.moveTo(pos.x, 0);
                this.ctx.lineTo(pos.x, this.height);
                this.ctx.stroke();
                
                // Label
                this.ctx.fillStyle = this.config.textColor;
                this.ctx.font = `${this.config.fontSize}px Arial`;
                this.ctx.textAlign = 'center';
                this.ctx.fillText(`${ra.toFixed(1)}h`, pos.x, 20);
            }
        }
        
        // Dec lines (horizontal)
        const decStep = 10; // degrees
        for (let dec = -80; dec <= 80; dec += decStep) {
            if (Math.abs(dec - this.config.centerDec) <= this.config.fovHeight / 2) {
                const pos = this.raDecToCanvas(this.config.centerRA, dec);
                this.ctx.beginPath();
                this.ctx.moveTo(0, pos.y);
                this.ctx.lineTo(this.width, pos.y);
                this.ctx.stroke();
                
                // Label
                this.ctx.fillStyle = this.config.textColor;
                this.ctx.font = `${this.config.fontSize}px Arial`;
                this.ctx.textAlign = 'left';
                this.ctx.fillText(`${dec}°`, 10, pos.y);
            }
        }
    }
    
    // Draw constellation lines
    drawConstellations() {
        this.ctx.lineWidth = 2;
        
        this.constellations.forEach(line => {
            if (this.isInView(line.from.ra, line.from.dec) || this.isInView(line.to.ra, line.to.dec)) {
                const fromPos = this.raDecToCanvas(line.from.ra, line.from.dec);
                const toPos = this.raDecToCanvas(line.to.ra, line.to.dec);
                
                this.ctx.strokeStyle = line.color;
                this.ctx.beginPath();
                this.ctx.moveTo(fromPos.x, fromPos.y);
                this.ctx.lineTo(toPos.x, toPos.y);
                this.ctx.stroke();
            }
        });
    }
    
    // Draw stars
    drawStars() {
        this.stars.forEach(star => {
            if (this.isInView(star.ra, star.dec)) {
                const pos = this.raDecToCanvas(star.ra, star.dec);
                
                // Calculate star size based on magnitude
                const size = Math.max(1, this.config.starSize * (6 - star.magnitude) / 5);
                
                // Draw star
                this.ctx.fillStyle = this.config.starColor;
                this.ctx.beginPath();
                this.ctx.arc(pos.x, pos.y, size, 0, Math.PI * 2);
                this.ctx.fill();
                
                // Draw star name for bright stars
                if (star.name && star.magnitude < 2) {
                    this.ctx.fillStyle = this.config.textColor;
                    this.ctx.font = `${this.config.fontSize}px Arial`;
                    this.ctx.textAlign = 'left';
                    this.ctx.fillText(star.name, pos.x + 10, pos.y);
                }
            }
        });
    }
    
    // Draw mount position
    drawMountPosition() {
        if (this.isInView(this.mountPosition.ra, this.mountPosition.dec)) {
            const pos = this.raDecToCanvas(this.mountPosition.ra, this.mountPosition.dec);
            
            // Draw crosshair
            this.ctx.strokeStyle = this.config.mountColor;
            this.ctx.lineWidth = 2;
            
            // Crosshair lines
            this.ctx.beginPath();
            this.ctx.moveTo(pos.x - 10, pos.y);
            this.ctx.lineTo(pos.x + 10, pos.y);
            this.ctx.moveTo(pos.x, pos.y - 10);
            this.ctx.lineTo(pos.x, pos.y + 10);
            this.ctx.stroke();
            
            // Circle
            this.ctx.beginPath();
            this.ctx.arc(pos.x, pos.y, this.config.mountSize, 0, Math.PI * 2);
            this.ctx.stroke();
            
            // Label
            this.ctx.fillStyle = this.config.mountColor;
            this.ctx.font = `${this.config.fontSize}px Arial`;
            this.ctx.textAlign = 'right';
            this.ctx.fillText('Mount', pos.x - 10, pos.y - 10);
        }
    }
    
    // Draw target position
    drawTargetPosition() {
        if (this.isInView(this.targetPosition.ra, this.targetPosition.dec)) {
            const pos = this.raDecToCanvas(this.targetPosition.ra, this.targetPosition.dec);
            
            // Draw target circle
            this.ctx.strokeStyle = this.config.targetColor;
            this.ctx.lineWidth = 2;
            
            this.ctx.beginPath();
            this.ctx.arc(pos.x, pos.y, this.config.targetSize, 0, Math.PI * 2);
            this.ctx.stroke();
            
            // Draw cross
            this.ctx.beginPath();
            this.ctx.moveTo(pos.x - 8, pos.y);
            this.ctx.lineTo(pos.x - 3, pos.y);
            this.ctx.moveTo(pos.x + 3, pos.y);
            this.ctx.lineTo(pos.x + 8, pos.y);
            this.ctx.moveTo(pos.x, pos.y - 8);
            this.ctx.lineTo(pos.x, pos.y - 3);
            this.ctx.moveTo(pos.x, pos.y + 3);
            this.ctx.lineTo(pos.x, pos.y + 8);
            this.ctx.stroke();
            
            // Label
            this.ctx.fillStyle = this.config.targetColor;
            this.ctx.font = `${this.config.fontSize}px Arial`;
            this.ctx.textAlign = 'left';
            this.ctx.fillText('Target', pos.x + 10, pos.y + 10);
        }
    }
    
    // Draw coordinate labels
    drawLabels() {
        this.ctx.fillStyle = this.config.textColor;
        this.ctx.font = 'bold 14px Arial';
        this.ctx.textAlign = 'center';
        
        // Title
        this.ctx.fillText('Sky Map', this.width / 2, 30);
        
        // FOV info
        this.ctx.font = '12px Arial';
        this.ctx.fillText(
            `FOV: ${this.config.fovWidth}° × ${this.config.fovHeight}°`, 
            this.width / 2, 
            this.height - 60
        );
        
        // Center coordinates
        this.ctx.fillText(
            `Center: RA ${this.config.centerRA.toFixed(2)}h, Dec ${this.config.centerDec.toFixed(2)}°`,
            this.width / 2,
            this.height - 40
        );
        
        // Mount coordinates
        this.ctx.fillText(
            `Mount: RA ${this.mountPosition.ra.toFixed(2)}h, Dec ${this.mountPosition.dec.toFixed(2)}°`,
            this.width / 2,
            this.height - 20
        );
    }
    
    // Update mount position
    updateMountPosition(ra, dec) {
        this.mountPosition.ra = this.normalizeRA(ra);
        this.mountPosition.dec = dec;
        this.draw();
    }
    
    // Set target position
    setTargetPosition(ra, dec) {
        this.targetPosition = {
            ra: this.normalizeRA(ra),
            dec: dec
        };
        this.draw();
    }
    
    // Clear target position
    clearTargetPosition() {
        this.targetPosition = null;
        this.draw();
    }
    
    // Refresh the sky map
    refresh() {
        this.draw();
    }
    
    // Change center coordinates
    setCenter(ra, dec) {
        this.config.centerRA = this.normalizeRA(ra);
        this.config.centerDec = Math.max(-90, Math.min(90, dec));
        this.draw();
    }
    
    // Change field of view
    setFOV(width, height) {
        this.config.fovWidth = Math.max(1, Math.min(180, width));
        this.config.fovHeight = Math.max(1, Math.min(90, height));
        this.draw();
    }
    
    // Toggle grid visibility
    toggleGrid() {
        this.config.showGrid = !this.config.showGrid;
        this.draw();
    }
    
    // Toggle star visibility
    toggleStars() {
        this.config.showStars = !this.config.showStars;
        this.draw();
    }
    
    // Toggle constellation visibility
    toggleConstellations() {
        this.config.showConstellations = !this.config.showConstellations;
        this.draw();
    }
    
    // Zoom in
    zoomIn() {
        this.config.fovWidth = Math.max(1, this.config.fovWidth * 0.8);
        this.config.fovHeight = Math.max(1, this.config.fovHeight * 0.8);
        this.draw();
    }
    
    // Zoom out
    zoomOut() {
        this.config.fovWidth = Math.min(180, this.config.fovWidth * 1.2);
        this.config.fovHeight = Math.min(90, this.config.fovHeight * 1.2);
        this.draw();
    }
    
    // Pan the view
    pan(deltaRA, deltaDec) {
        this.config.centerRA = this.normalizeRA(this.config.centerRA + deltaRA);
        this.config.centerDec = Math.max(-90, Math.min(90, this.config.centerDec + deltaDec));
        this.draw();
    }
    
    // Center on mount position
    centerOnMount() {
        this.setCenter(this.mountPosition.ra, this.mountPosition.dec);
    }
    
    // Get current view parameters
    getViewInfo() {
        return {
            centerRA: this.config.centerRA,
            centerDec: this.config.centerDec,
            fovWidth: this.config.fovWidth,
            fovHeight: this.config.fovHeight,
            mountPosition: { ...this.mountPosition },
            targetPosition: this.targetPosition ? { ...this.targetPosition } : null
        };
    }
}

// Make SkyMap available globally
if (typeof window !== 'undefined') {
    window.SkyMap = SkyMap;
}