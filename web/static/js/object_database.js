/**
 * Object Database Module for Astronomical Mount Controller
 * Provides UI and functionality for browsing astronomical objects from database
 */

class ObjectDatabaseManager {
    constructor(mountControllerApp) {
        this.app = mountControllerApp;
        this.currentPage = 1;
        this.totalPages = 1;
        this.pageSize = 20;
        this.selectedObject = null;
        this.objects = [];
        
        // Object type names mapping
        this.objectTypeNames = {
            0: 'Unknown',
            1: 'Star',
            2: 'Planet', 
            3: 'Galaxy',
            4: 'Nebula',
            5: 'Star Cluster',
            6: 'Double Star',
            7: 'Variable Star',
            8: 'Asteroid',
            9: 'Comet',
            10: 'Satellite',
            11: 'Exoplanet',
            12: 'Quasar',
            13: 'Pulsar',
            14: 'Black Hole',
            15: 'Other'
        };
        
        // Initialize event listeners
        this.initEventListeners();
    }
    
    // Initialize event listeners for object database UI
    initEventListeners() {
        // Search button and enter key
        const searchBtn = document.getElementById('search-objects-btn');
        const searchInput = document.getElementById('object-search');
        
        if (searchBtn) {
            searchBtn.addEventListener('click', () => this.searchObjects());
        }
        
        if (searchInput) {
            searchInput.addEventListener('keypress', (e) => {
                if (e.key === 'Enter') {
                    this.searchObjects();
                }
            });
        }
        
        // Pagination buttons
        const prevBtn = document.getElementById('prev-page-btn');
        const nextBtn = document.getElementById('next-page-btn');
        
        if (prevBtn) {
            prevBtn.addEventListener('click', () => this.prevPage());
        }
        
        if (nextBtn) {
            nextBtn.addEventListener('click', () => this.nextPage());
        }
        
        // Object action buttons
        const useCoordsBtn = document.getElementById('use-object-coords-btn');
        const trackBtn = document.getElementById('track-object-btn');
        const favoriteBtn = document.getElementById('add-to-favorites-btn');
        
        if (useCoordsBtn) {
            useCoordsBtn.addEventListener('click', () => this.useObjectCoordinates());
        }
        
        if (trackBtn) {
            trackBtn.addEventListener('click', () => this.trackSelectedObject());
        }
        
        if (favoriteBtn) {
            favoriteBtn.addEventListener('click', () => this.addToFavorites());
        }
    }
    
    // Search for astronomical objects
    async searchObjects() {
        const searchQuery = document.getElementById('object-search').value.trim();
        const objectType = parseInt(document.getElementById('object-type-filter').value) || 0;
        const maxMagnitude = parseFloat(document.getElementById('magnitude-filter').value) || 10.0;
        
        // Show loading state
        this.showLoadingState();
        
        try {
            let url = `/api/db/objects?page=${this.currentPage}&pageSize=${this.pageSize}`;
            
            // Build query parameters
            const params = new URLSearchParams();
            if (searchQuery) {
                params.append('search', searchQuery);
            }
            if (objectType > 0) {
                params.append('type', objectType);
            }
            if (maxMagnitude < 20) {
                params.append('maxMagnitude', maxMagnitude);
            }
            
            const queryString = params.toString();
            if (queryString) {
                url += '&' + queryString;
            }
            
            const response = await fetch(url);
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            
            const data = await response.json();
            this.objects = data.objects || [];
            this.totalPages = data.total_pages || 1;
            this.totalObjects = data.total_count || 0;
            
            this.renderObjectList();
            this.updatePaginationControls();
            
            // Log success
            this.app.log(`Found ${this.totalObjects} objects`, 'success');
            
        } catch (error) {
            this.app.log(`Failed to search objects: ${error.message}`, 'error');
            this.showErrorState('Failed to load objects. Please check connection to database service.');
        }
    }
    
    // Show loading state in object list
    showLoadingState() {
        const objectList = document.getElementById('object-list');
        if (objectList) {
            objectList.innerHTML = `
                <div class="object-list-loading">
                    <div class="loading-spinner"></div>
                </div>
            `;
        }
        
        // Disable pagination buttons
        this.updatePaginationControls(true);
    }
    
    // Show error state in object list
    showErrorState(message) {
        const objectList = document.getElementById('object-list');
        if (objectList) {
            objectList.innerHTML = `
                <div class="object-list-empty">
                    <i class="fas fa-exclamation-triangle"></i>
                    <p>${message}</p>
                    <button class="btn btn-small btn-primary" onclick="window.mountControllerApp.objectDB.searchObjects()">
                        Try Again
                    </button>
                </div>
            `;
        }
    }
    
    // Render the list of objects
    renderObjectList() {
        const objectList = document.getElementById('object-list');
        
        if (!objectList) return;
        
        if (this.objects.length === 0) {
            objectList.innerHTML = `
                <div class="object-list-empty">
                    <i class="fas fa-search"></i>
                    <p>No objects found matching your criteria</p>
                </div>
            `;
            return;
        }
        
        let html = '';
        
        this.objects.forEach((obj, index) => {
            const typeName = this.objectTypeNames[obj.object_type] || 'Unknown';
            const typeClass = this.getObjectTypeClass(obj.object_type);
            const magnitudeClass = this.getMagnitudeClass(obj.v_magnitude);
            const raFormatted = this.formatRA(obj.ra_hours);
            const decFormatted = this.formatDec(obj.dec_degrees);
            
            html += `
                <div class="object-item ${this.selectedObject && this.selectedObject.id === obj.id ? 'selected' : ''}" 
                     data-index="${index}" data-object-id="${obj.id}">
                    <div class="object-name" title="${obj.name || ''}">
                        ${obj.name || 'Unnamed Object'}
                    </div>
                    <div class="object-type ${typeClass}">
                        ${typeName}
                    </div>
                    <div class="object-magnitude ${magnitudeClass}">
                        ${obj.v_magnitude !== undefined && obj.v_magnitude !== null ? obj.v_magnitude.toFixed(1) : '-'}
                    </div>
                    <div class="object-coordinates">
                        ${raFormatted} / ${decFormatted}
                    </div>
                </div>
            `;
        });
        
        objectList.innerHTML = html;
        
        // Add click handlers to object items
        const objectItems = objectList.querySelectorAll('.object-item');
        objectItems.forEach(item => {
            item.addEventListener('click', (e) => {
                const index = parseInt(item.getAttribute('data-index'));
                this.selectObject(index);
            });
        });
    }
    
    // Select an object from the list
    selectObject(index) {
        if (index < 0 || index >= this.objects.length) {
            return;
        }
        
        this.selectedObject = this.objects[index];
        
        // Update UI selection
        const objectItems = document.querySelectorAll('.object-item');
        objectItems.forEach(item => item.classList.remove('selected'));
        
        const selectedItem = document.querySelector(`.object-item[data-index="${index}"]`);
        if (selectedItem) {
            selectedItem.classList.add('selected');
        }
        
        // Update selected object details
        this.renderSelectedObjectDetails();
        
        // Enable action buttons
        const useCoordsBtn = document.getElementById('use-object-coords-btn');
        const trackBtn = document.getElementById('track-object-btn');
        const favoriteBtn = document.getElementById('add-to-favorites-btn');
        
        if (useCoordsBtn) useCoordsBtn.disabled = false;
        if (trackBtn) trackBtn.disabled = false;
        if (favoriteBtn) favoriteBtn.disabled = false;
    }
    
    // Render details of selected object
    renderSelectedObjectDetails() {
        const detailsContainer = document.getElementById('selected-object-details');
        
        if (!detailsContainer || !this.selectedObject) {
            return;
        }
        
        const obj = this.selectedObject;
        const typeName = this.objectTypeNames[obj.object_type] || 'Unknown';
        const magnitude = obj.v_magnitude !== undefined && obj.v_magnitude !== null ? obj.v_magnitude.toFixed(1) : 'N/A';
        const raFormatted = this.formatRA(obj.ra_hours, true);
        const decFormatted = this.formatDec(obj.dec_degrees, true);
        const sizeFormatted = obj.angular_size ? `${obj.angular_size.toFixed(2)}°` : 'N/A';
        
        let html = '';
        
        // Basic info
        html += `
            <div class="object-detail">
                <span class="detail-label">Name:</span>
                <span class="detail-value">${obj.name || 'Unnamed'}</span>
            </div>
            <div class="object-detail">
                <span class="detail-label">Catalog:</span>
                <span class="detail-value">${obj.catalog_name || 'N/A'}</span>
            </div>
            <div class="object-detail">
                <span class="detail-label">Type:</span>
                <span class="detail-value ${this.getObjectTypeClass(obj.object_type)}">${typeName}</span>
            </div>
            <div class="object-detail">
                <span class="detail-label">Magnitude:</span>
                <span class="detail-value ${this.getMagnitudeClass(obj.v_magnitude)}">${magnitude}</span>
            </div>
        `;
        
        // Coordinates
        html += `
            <div class="object-detail">
                <span class="detail-label">Right Ascension:</span>
                <span class="detail-value">${raFormatted}</span>
            </div>
            <div class="object-detail">
                <span class="detail-label">Declination:</span>
                <span class="detail-value">${decFormatted}</span>
            </div>
        `;
        
        // Additional info if available
        if (obj.constellation) {
            html += `
                <div class="object-detail">
                    <span class="detail-label">Constellation:</span>
                    <span class="detail-value">${obj.constellation}</span>
                </div>
            `;
        }
        
        if (obj.angular_size) {
            html += `
                <div class="object-detail">
                    <span class="detail-label">Angular Size:</span>
                    <span class="detail-value">${sizeFormatted}</span>
                </div>
            `;
        }
        
        if (obj.distance_ly) {
            html += `
                <div class="object-detail">
                    <span class="detail-label">Distance:</span>
                    <span class="detail-value">${obj.distance_ly.toLocaleString()} ly</span>
                </div>
            `;
        }
        
        detailsContainer.innerHTML = html;
    }
    
    // Use selected object coordinates for slewing
    async useObjectCoordinates() {
        if (!this.selectedObject) {
            this.app.log('No object selected', 'warning');
            return;
        }
        
        const obj = this.selectedObject;
        
        // Update coordinate inputs
        const raInput = document.getElementById('target-ra');
        const decInput = document.getElementById('target-dec');
        const nameInput = document.getElementById('object-name');
        
        if (raInput && obj.ra_hours !== undefined && obj.ra_hours !== null) {
            raInput.value = obj.ra_hours.toFixed(6);
        }
        
        if (decInput && obj.dec_degrees !== undefined && obj.dec_degrees !== null) {
            decInput.value = obj.dec_degrees.toFixed(6);
        }
        
        if (nameInput && obj.name) {
            nameInput.value = obj.name;
        }
        
        this.app.log(`Loaded coordinates for ${obj.name || 'selected object'}`, 'success');
        
        // Auto-slew if configured to do so
        const autoSlew = false; // Could be configurable
        if (autoSlew) {
            await this.slewToSelectedObject();
        }
    }
    
    // Slew to selected object
    async slewToSelectedObject() {
        if (!this.selectedObject) {
            this.app.log('No object selected', 'warning');
            return;
        }
        
        const obj = this.selectedObject;
        
        try {
            // Use the app's existing slew functionality
            await this.app.slewToCoordinates({
                ra: obj.ra_hours,
                dec: obj.dec_degrees,
                objectName: obj.name || ''
            });
            
            this.app.log(`Slewing to ${obj.name || 'selected object'}`, 'success');
            
        } catch (error) {
            this.app.log(`Failed to slew to object: ${error.message}`, 'error');
        }
    }
    
    // Track selected object
    async trackSelectedObject() {
        if (!this.selectedObject) {
            this.app.log('No object selected', 'warning');
            return;
        }
        
        const obj = this.selectedObject;
        
        try {
            // Use the app's existing track functionality
            await this.app.trackObject({
                ra: obj.ra_hours,
                dec: obj.dec_degrees
            });
            
            this.app.log(`Tracking ${obj.name || 'selected object'}`, 'success');
            
        } catch (error) {
            this.app.log(`Failed to track object: ${error.message}`, 'error');
        }
    }
    
    // Add selected object to favorites
    async addToFavorites() {
        if (!this.selectedObject) {
            this.app.log('No object selected', 'warning');
            return;
        }
        
        const obj = this.selectedObject;
        
        try {
            const response = await fetch(`/api/db/favorites/${obj.id}`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    userId: 'web_user' // Could be configurable
                })
            });
            
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            
            this.app.log(`Added ${obj.name || 'object'} to favorites`, 'success');
            
            // Update button state
            const favoriteBtn = document.getElementById('add-to-favorites-btn');
            if (favoriteBtn) {
                favoriteBtn.innerHTML = '<i class="fas fa-star"></i> Favorited';
                favoriteBtn.disabled = true;
                favoriteBtn.classList.remove('btn-info');
                favoriteBtn.classList.add('btn-success');
            }
            
        } catch (error) {
            this.app.log(`Failed to add to favorites: ${error.message}`, 'error');
        }
    }
    
    // Pagination controls
    prevPage() {
        if (this.currentPage > 1) {
            this.currentPage--;
            this.searchObjects();
        }
    }
    
    nextPage() {
        if (this.currentPage < this.totalPages) {
            this.currentPage++;
            this.searchObjects();
        }
    }
    
    updatePaginationControls(isLoading = false) {
        const prevBtn = document.getElementById('prev-page-btn');
        const nextBtn = document.getElementById('next-page-btn');
        const pageInfo = document.getElementById('page-info');
        
        if (prevBtn) {
            prevBtn.disabled = isLoading || this.currentPage <= 1;
        }
        
        if (nextBtn) {
            nextBtn.disabled = isLoading || this.currentPage >= this.totalPages;
        }
        
        if (pageInfo) {
            pageInfo.textContent = isLoading ? 'Loading...' : `Page ${this.currentPage} of ${this.totalPages}`;
        }
    }
    
    // Helper methods
    
    getObjectTypeClass(type) {
        const typeClasses = {
            1: 'object-type-star',
            2: 'object-type-planet',
            3: 'object-type-galaxy',
            4: 'object-type-nebula',
            5: 'object-type-cluster',
            6: 'object-type-double',
            7: 'object-type-variable',
            8: 'object-type-asteroid',
            9: 'object-type-comet',
            10: 'object-type-satellite',
            11: 'object-type-exoplanet',
            12: 'object-type-quasar',
            13: 'object-type-pulsar',
            14: 'object-type-black-hole',
            15: 'object-type-other'
        };
        
        return typeClasses[type] || 'object-type-other';
    }
    
    getMagnitudeClass(magnitude) {
        if (magnitude === undefined || magnitude === null) return '';
        
        if (magnitude <= 1.0) return 'magnitude-bright';
        if (magnitude <= 5.0) return 'magnitude-medium';
        return 'magnitude-faint';
    }
    
    formatRA(raHours, fullFormat = false) {
        if (raHours === undefined || raHours === null) return 'N/A';
        
        if (fullFormat) {
            const hours = Math.floor(raHours);
            const minutes = Math.floor((raHours - hours) * 60);
            const seconds = ((raHours - hours - minutes/60) * 3600).toFixed(1);
            return `${hours.toString().padStart(2, '0')}h ${minutes.toString().padStart(2, '0')}m ${seconds}s`;
        } else {
            return raHours.toFixed(3) + 'h';
        }
    }
    
    formatDec(decDegrees, fullFormat = false) {
        if (decDegrees === undefined || decDegrees === null) return 'N/A';
        
        const sign = decDegrees >= 0 ? '+' : '-';
        const absDec = Math.abs(decDegrees);
        
        if (fullFormat) {
            const degrees = Math.floor(absDec);
            const minutes = Math.floor((absDec - degrees) * 60);
            const seconds = ((absDec - degrees - minutes/60) * 3600).toFixed(1);
            return `${sign}${degrees.toString().padStart(2, '0')}° ${minutes.toString().padStart(2, '0')}' ${seconds}"`;
        } else {
            return sign + absDec.toFixed(2) + '°';
        }
    }
    
    // Initialize database connection check
    async checkDatabaseConnection() {
        try {
            const response = await fetch('/api/health');
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            
            const data = await response.json();
            const dbStatus = data.services?.object_database?.status;
            
            if (dbStatus === 'connected') {
                this.app.log('Object database service connected', 'success');
                return true;
            } else {
                this.app.log('Object database service not available', 'warning');
                return false;
            }
            
        } catch (error) {
            this.app.log('Cannot connect to object database service', 'warning');
            return false;
        }
    }
}

// Export for use in main app
window.ObjectDatabaseManager = ObjectDatabaseManager;