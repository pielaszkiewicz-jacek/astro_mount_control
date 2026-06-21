/**
 * Astronomical Mount Controller - Web Interface API Client
 *
 * HTTP/JSON client for communicating with the proxy server.
 * All methods return Promises for async/await usage.
 */

const Api = (() => {
  'use strict';

  const BASE_URL = '/api';

  /**
   * Generic fetch wrapper with error handling.
   * @param {string} path - API endpoint path
   * @param {object} [options] - Fetch options
   * @returns {Promise<object>}
   */
  async function request(path, options = {}) {
    const url = `${BASE_URL}${path}`;

    const defaultHeaders = {
      'Content-Type': 'application/json',
      'Accept': 'application/json',
    };

    // Extract timeout from options before spreading to fetch;
    // default is 30000ms for database queries; override for specific calls
    // (e.g. 120000ms for imports, 5000ms for quick status checks)
    const timeout = options.timeout || 30000;

    // Build fetch options without the custom 'timeout' property
    const fetchOpts = { ...options };
    delete fetchOpts.timeout;

    try {
      const response = await fetch(url, {
        headers: { ...defaultHeaders, ...options.headers },
        signal: AbortSignal.timeout(timeout),
        ...fetchOpts,
        body: options.body
          ? (typeof options.body === 'string' ? options.body : JSON.stringify(options.body))
          : undefined,
      });

      const data = await response.json();

      if (!response.ok) {
        // Include details from proxy error response when available
        const msg = data.details ? `${data.error}: ${data.details}` : (data.error || `HTTP ${response.status}`);
        throw new Error(msg);
      }

      return data;
    } catch (err) {
      if (err.name === 'TimeoutError') {
        throw new Error('Request timed out');
      }
      if (err.name === 'AbortError') {
        throw new Error('Request aborted');
      }
      throw err;
    }
  }

  /**
   * Get the current mount controller state.
   * GET /api/status
   * @returns {Promise<object>}
   */
  async function getStatus() {
    return request('/status');
  }

  /**
   * Slew to equatorial coordinates.
   * POST /api/slew
   * @param {number} ra - Right ascension in hours
   * @param {number} dec - Declination in degrees
   * @returns {Promise<object>}
   */
  async function slewToCoordinates(ra, dec) {
    return request('/slew', {
      method: 'POST',
      body: JSON.stringify({ ra, dec }),
    });
  }

  /**
   * Stop all mount movement.
   * POST /api/stop
   * @returns {Promise<object>}
   */
  async function stopMount() {
    return request('/stop', { method: 'POST' });
  }

  /**
   * Park the mount.
   * POST /api/park
   * @returns {Promise<object>}
   */
  async function parkMount() {
    return request('/park', { method: 'POST' });
  }

  /**
   * Unpark the mount.
   * POST /api/unpark
   * @returns {Promise<object>}
   */
  async function unparkMount() {
    return request('/unpark', { method: 'POST' });
  }

  /**
   * Clear errors in the mount controller.
   * POST /api/clear-errors
   * @returns {Promise<object>}
   */
  async function clearErrors() {
    return request('/clear-errors', { method: 'POST' });
  }

  /**
   * Save the current mount controller state (position, calibration, tracking).
   * POST /api/state/save
   * @param {object} [options] - Optional save parameters
   * @param {string} [options.file_path] - Custom file path (empty = default location)
   * @param {boolean} [options.include_measurements] - Include calibration measurements
   * @returns {Promise<{success:boolean, message:string, file_path:string, file_size:number}>}
   */
  async function saveState(options = {}) {
    return request('/state/save', {
      method: 'POST',
      body: JSON.stringify(options),
    });
  }

  /**
   * Load and restore a previously saved mount controller state.
   * POST /api/state/load
   * @param {object} [options] - Optional load parameters
   * @param {string} [options.file_path] - Path to state file (empty = default location)
   * @returns {Promise<{success:boolean, message:string}>}
   */
  async function loadState(options = {}) {
    return request('/state/load', {
      method: 'POST',
      body: JSON.stringify(options),
    });
  }

  /**
   * Upload a state file from the user's computer and restore it to the mount controller.
   * POST /api/state/upload-and-load
   * @param {string} fileContent - The full text content of the state JSON file
   * @param {string} fileName - The original filename for reference
   * @returns {Promise<{success:boolean, message:string}>}
   */
  async function uploadAndLoadState(fileContent, fileName) {
    return request('/state/upload-and-load', {
      method: 'POST',
      body: JSON.stringify({ file_content: fileContent, file_name: fileName }),
    });
  }

  /**
   * Get the mount controller configuration.
   * GET /api/config
   * @returns {Promise<object>}
   */
  async function getConfig() {
    return request('/config');
  }

  /**
   * Update the mount controller configuration.
   * POST /api/config
   * @param {object} configData - Configuration object with fields to update
   * @returns {Promise<object>}
   */
  async function updateConfig(configData) {
    return request('/config', {
      method: 'POST',
      body: JSON.stringify(configData),
    });
  }

  /**
   * Reset all configuration to factory defaults.
   * POST /api/config/reset
   * @returns {Promise<object>}
   */
  async function resetConfig() {
    return request('/config/reset', { method: 'POST' });
  }

  /**
   * Reset a specific configuration group to defaults.
   * POST /api/config/reset-group
   * @param {string} groupName - Name of the config group (e.g., 'logging', 'network', 'mount')
   * @returns {Promise<object>}
   */
  async function resetGroupConfig(groupName) {
    return request('/config/reset-group', {
      method: 'POST',
      body: JSON.stringify({ group: groupName }),
    });
  }

  /**
   * Get current gRPC addresses for controller and database.
   * GET /api/config/addresses
   * @returns {Promise<{controller: {host:string, port:number}, database: {host:string, port:number}}>}
   */
  async function getAddresses() {
    return request('/config/addresses');
  }

  /**
   * Update gRPC addresses and reconnect clients.
   * POST /api/config/addresses
   * @param {object} options
   * @param {{host:string, port:number}} [options.controller] - Mount controller address
   * @param {{host:string, port:number}} [options.database]  - Database address
   * @returns {Promise<object>}
   */
  async function setAddresses(options = {}) {
    return request('/config/addresses', {
      method: 'POST',
      body: JSON.stringify(options),
    });
  }

  /**
   * Health check.
   * GET /api/health
   * @returns {Promise<object>}
   */
  async function checkHealth() {
    return request('/health');
  }

  /**
   * Move an axis at a given velocity (low-level control).
   * POST /api/axis/move
   * @param {number} axisId - 0=HA/RA/Azimuth, 1=Dec/Altitude
   * @param {number} velocity - Velocity in deg/s (positive=forward, negative=backward)
   * @param {number} [acceleration] - Acceleration in deg/s² (default: 50)
   * @returns {Promise<object>}
   */
  async function moveAxis(axisId, velocity, acceleration) {
    const body = { axis_id: axisId, velocity };
    if (acceleration !== undefined && acceleration > 0) {
      body.acceleration = acceleration;
    }
    return request('/axis/move', {
      method: 'POST',
      body: JSON.stringify(body),
    });
  }

  /**
   * Move axis by a relative offset using position control.
   * POST /api/axis/move-relative
   * @param {number} axisId - 0=HA/RA/Azimuth, 1=Dec/Altitude
   * @param {number} offsetDeg - Relative offset in degrees
   * @param {number} [velocity] - Maximum velocity in deg/s (uses config max_slew_rate if omitted)
   * @param {number} [acceleration] - Acceleration in deg/s² (default: 50)
   * @param {number} [deceleration] - Deceleration in deg/s² (default: 50)
   * @returns {Promise<object>}
   */
  async function moveAxisRelative(axisId, offsetDeg, velocity, acceleration, deceleration) {
    const body = { axis_id: axisId, offset_deg: offsetDeg };
    if (velocity !== undefined && velocity > 0) {
      body.velocity = velocity;
    }
    if (acceleration !== undefined && acceleration > 0) {
      body.acceleration = acceleration;
    }
    if (deceleration !== undefined && deceleration > 0) {
      body.deceleration = deceleration;
    }
    return request('/axis/move-relative', {
      method: 'POST',
      body: JSON.stringify(body),
    });
  }

  /**
   * Stop axis movement with smooth deceleration.
   * POST /api/axis/stop
   * @param {number} axisId - 0=HA/RA/Azimuth, 1=Dec/Altitude
   * @param {number} [deceleration] - Deceleration rate in deg/s² (default: from config)
   * @returns {Promise<object>}
   */
  async function stopAxis(axisId, deceleration) {
    const body = { axis_id: axisId };
    if (deceleration !== undefined && deceleration > 0) {
      body.deceleration = deceleration;
    }
    return request('/axis/stop', {
      method: 'POST',
      body: JSON.stringify(body),
    });
  }

  /**
   * Get HAL configuration (flattened).
   * GET /api/hal/config
   * @returns {Promise<object>}
   */
  async function getHALConfig() {
    return request('/hal/config');
  }

  /**
   * Update HAL configuration.
   * POST /api/hal/config
   * @param {object} halData - Flattened HAL config object
   * @returns {Promise<object>}
   */
  async function setHALConfig(halData) {
    return request('/hal/config', {
      method: 'POST',
      body: JSON.stringify(halData),
    });
  }

  /**
   * Emergency stop — halt all axes immediately.
   * POST /api/axis/emergency-stop
   * @returns {Promise<object>}
   */
  async function emergencyStop() {
    return request('/axis/emergency-stop', {
      method: 'POST',
      body: JSON.stringify({ axis_id: -1 }),
    });
  }

  /**
   * Get current mount orientation quaternion.
   * GET /api/mount/orientation
   * @returns {Promise<{qx: number, qy: number, qz: number, qw: number}>}
   */
  async function getMountOrientation() {
    return request('/mount/orientation');
  }

  /**
   * Set mount orientation quaternion (for CASUAL mount type).
   * POST /api/mount/orientation
   * @param {object} orient - { qx, qy, qz, qw }
   * @returns {Promise<object>}
   */
  async function setMountOrientation(orient) {
    return request('/mount/orientation', {
      method: 'POST',
      body: JSON.stringify(orient),
    });
  }

  /**
   * Slew to horizontal coordinates (altitude/azimuth).
   * POST /api/axis/slew-horizontal
   * @param {number} altitude - Altitude in degrees [0, 90]
   * @param {number} azimuth - Azimuth in degrees [0, 360]
   * @returns {Promise<object>}
   */
  async function slewHorizontal(altitude, azimuth) {
    return request('/axis/slew-horizontal', {
      method: 'POST',
      body: JSON.stringify({ altitude, azimuth }),
    });
  }

  // ─── Database API Methods ──────────────────────────────────────────────

  /**
   * Check database connection health.
   * GET /api/db/stats — returns true if reachable, false otherwise.
   * @returns {Promise<boolean>}
   */
  async function checkDbHealth() {
    try {
      await request('/db/stats');
      return true;
    } catch {
      return false;
    }
  }

  /**
   * Get database statistics.
   * GET /api/db/stats
   * @returns {Promise<object>}
   */
  async function getDbStats() {
    return request('/db/stats');
  }

  /**
   * List objects with pagination.
   * GET /api/db/objects
   * @param {object} [params] - Query parameters
   * @param {number} [params.page=1]
   * @param {number} [params.pageSize=20]
   * @param {string} [params.sortBy]
   * @param {boolean} [params.sortDescending]
   * @param {string} [params.filterType]
   * @param {number} [params.minMagnitude]
   * @param {number} [params.maxMagnitude]
   * @returns {Promise<object>}
   */
  async function listObjects(params = {}) {
    const searchParams = new URLSearchParams();
    if (params.page) searchParams.set('page', params.page);
    if (params.pageSize) searchParams.set('pageSize', params.pageSize);
    if (params.sortBy) searchParams.set('sortBy', params.sortBy);
    if (params.sortDescending) searchParams.set('sortDescending', 'true');
    if (params.filterType) searchParams.set('filterType', params.filterType);
    if (params.minMagnitude !== undefined) searchParams.set('minMagnitude', params.minMagnitude);
    if (params.maxMagnitude !== undefined) searchParams.set('maxMagnitude', params.maxMagnitude);
    const qs = searchParams.toString();
    return request(`/db/objects${qs ? '?' + qs : ''}`);
  }

  /**
   * Search objects by query.
   * GET /api/db/objects/search
   * @param {object} params - Search parameters
   * @param {string} params.query
   * @param {string} [params.objectType]
   * @param {number} [params.minMagnitude]
   * @param {number} [params.maxMagnitude]
   * @returns {Promise<object>}
   */
  async function searchObjects(params = {}) {
    const searchParams = new URLSearchParams();
    if (params.query) searchParams.set('query', params.query);
    if (params.objectType) searchParams.set('objectType', params.objectType);
    if (params.minMagnitude !== undefined) searchParams.set('minMagnitude', params.minMagnitude);
    if (params.maxMagnitude !== undefined) searchParams.set('maxMagnitude', params.maxMagnitude);
    if (params.favoritesOnly) searchParams.set('favoritesOnly', 'true');
    if (params.visibleOnly) searchParams.set('visibleOnly', 'true');
    if (params.catalogs) searchParams.set('catalogs', params.catalogs.join(','));
    if (params.constellation) searchParams.set('constellation', params.constellation);
    const qs = searchParams.toString();
    return request(`/db/objects/search${qs ? '?' + qs : ''}`);
  }

  /**
   * Get a single object by ID.
   * GET /api/db/objects/:id
   * @param {string} id - Object ID
   * @returns {Promise<object>}
   */
  async function getObject(id) {
    return request(`/db/objects/${encodeURIComponent(id)}`);
  }

  /**
   * Create a new astronomical object.
   * POST /api/db/objects
   * @param {object} objectData - AstronomicalObject fields
   * @returns {Promise<object>}
   */
  async function createObject(objectData) {
    return request('/db/objects', {
      method: 'POST',
      body: JSON.stringify(objectData),
    });
  }

  /**
   * Update an existing object.
   * PUT /api/db/objects/:id
   * @param {string} id - Object ID
   * @param {object} objectData - Fields to update
   * @returns {Promise<object>}
   */
  async function updateObject(id, objectData) {
    return request(`/db/objects/${encodeURIComponent(id)}`, {
      method: 'PUT',
      body: JSON.stringify(objectData),
    });
  }

  /**
   * Delete an object by ID.
   * DELETE /api/db/objects/:id
   * @param {string} id - Object ID
   * @returns {Promise<object>}
   */
  async function deleteObject(id) {
    return request(`/db/objects/${encodeURIComponent(id)}`, {
      method: 'DELETE',
    });
  }

  /**
   * List all favorite objects.
   * GET /api/db/favorites
   * @returns {Promise<object>}
   */
  async function getFavorites() {
    return request('/db/favorites');
  }

  /**
   * Add an object to favorites.
   * POST /api/db/favorites
   * @param {string} objectId
   * @returns {Promise<object>}
   */
  async function addFavorite(objectId) {
    return request('/db/favorites', {
      method: 'POST',
      body: JSON.stringify({ object_id: objectId }),
    });
  }

  /**
   * Remove an object from favorites.
   * DELETE /api/db/favorites/:id
   * @param {string} objectId
   * @returns {Promise<object>}
   */
  async function removeFavorite(objectId) {
    return request(`/db/favorites/${encodeURIComponent(objectId)}`, {
      method: 'DELETE',
    });
  }

  /**
   * List all categories.
   * GET /api/db/categories
   * @returns {Promise<object>}
   */
  async function listCategories() {
    return request('/db/categories');
  }

  /**
   * Create a new category.
   * POST /api/db/categories
   * @param {object} categoryData - { name, description?, color?, icon? }
   * @returns {Promise<object>}
   */
  async function createCategory(categoryData) {
    return request('/db/categories', {
      method: 'POST',
      body: JSON.stringify(categoryData),
    });
  }

  /**
   * Get tonight's best objects for observation.
   * GET /api/db/tonight
   * @param {object} [params] - { latitude?, longitude?, altitude?, maxResults? }
   * @returns {Promise<object>}
   */
  async function getTonightBest(params = {}) {
    const searchParams = new URLSearchParams();
    if (params.latitude) searchParams.set('latitude', params.latitude);
    if (params.longitude) searchParams.set('longitude', params.longitude);
    if (params.altitude) searchParams.set('altitude', params.altitude);
    if (params.maxResults) searchParams.set('maxResults', params.maxResults);
    if (params.minMagnitude !== undefined) searchParams.set('minMagnitude', params.minMagnitude);
    if (params.maxMagnitude !== undefined) searchParams.set('maxMagnitude', params.maxMagnitude);
    const qs = searchParams.toString();
    return request(`/db/tonight${qs ? '?' + qs : ''}`);
  }

  // ─── Calibration API Methods ────────────────────────────────────────────

  /**
   * Get bootstrap calibration status.
   * GET /api/calibration/bootstrap/status
   * @returns {Promise<object>}
   */
  async function getBootstrapStatus() {
    return request('/calibration/bootstrap/status');
  }

  /**
   * Add a bootstrap measurement for initial alignment.
   * POST /api/calibration/bootstrap/measurements
   * @param {object} measurement - BootstrapMeasurement fields
   * @returns {Promise<object>}
   */
  async function addBootstrapMeasurement(measurement) {
    return request('/calibration/bootstrap/measurements', {
      method: 'POST',
      body: JSON.stringify(measurement),
    });
  }

  /**
   * Run bootstrap calibration to compute initial alignment.
   * POST /api/calibration/bootstrap/run
   * @returns {Promise<object>}
   */
  async function runBootstrapCalibration() {
    return request('/calibration/bootstrap/run', { method: 'POST' });
  }

  /**
   * Clear all bootstrap measurements.
   * DELETE /api/calibration/bootstrap/measurements
   * @returns {Promise<object>}
   */
  async function clearBootstrapMeasurements() {
    return request('/calibration/bootstrap/measurements', { method: 'DELETE' });
  }

  /**
   * Get the current bootstrap mode.
   * GET /api/calibration/bootstrap/mode
   * @returns {Promise<{mode: number, encoder_type_absolute: boolean}>}
   */
  async function getBootstrapMode() {
    return request('/calibration/bootstrap/mode');
  }

  /**
   * Set the bootstrap mode.
   * POST /api/calibration/bootstrap/mode
   * @param {number} mode - 0=MANUAL, 1=HYBRID, 2=AUTOMATIC
   * @returns {Promise<{success: boolean, mode: number}>}
   */
  async function setBootstrapMode(mode) {
    return request('/calibration/bootstrap/mode', {
      method: 'POST',
      body: JSON.stringify({ mode }),
    });
  }

  /**
   * Start automatic bootstrap procedure.
   * POST /api/calibration/bootstrap/auto-run
   * @param {object} [options] - { target_star_names?, min_measurements?,
   *                               max_alignment_error_arcsec?, proceed_to_tpoint? }
   * @returns {Promise<{success: boolean, message: string}>}
   */
  async function runAutomaticBootstrap(options = {}) {
    return request('/calibration/bootstrap/auto-run', {
      method: 'POST',
      body: JSON.stringify(options),
    });
  }

  /**
   * Get automatic bootstrap status and progress.
   * GET /api/calibration/bootstrap/auto-status
   * @returns {Promise<object>} AutoBootstrapStatus
   */
  async function getAutoBootstrapStatus() {
    return request('/calibration/bootstrap/auto-status');
  }

  /**
   * Get TPOINT calibration parameters and status.
   * GET /api/calibration/tpoint/parameters
   * @returns {Promise<object>}
   */
  async function getTPointParameters() {
    return request('/calibration/tpoint/parameters');
  }

  /**
   * Add a TPOINT measurement for precise calibration.
   * POST /api/calibration/tpoint/measurements
   * @param {object} measurement - Measurement fields
   * @returns {Promise<object>}
   */
  async function addTPointMeasurement(measurement) {
    return request('/calibration/tpoint/measurements', {
      method: 'POST',
      body: JSON.stringify(measurement),
    });
  }

  /**
   * Run TPOINT calibration to compute precise pointing model.
   * POST /api/calibration/tpoint/run
   * @returns {Promise<object>}
   */
  async function runTPointCalibration() {
    return request('/calibration/tpoint/run', { method: 'POST' });
  }

  /**
   * Clear all TPOINT measurements.
   * DELETE /api/calibration/tpoint/measurements
   * @returns {Promise<object>}
   */
  async function clearTPointMeasurements() {
    return request('/calibration/tpoint/measurements', { method: 'DELETE' });
  }

  // ─── Ephemeris Tracking API Methods ─────────────────────────────────────────

  /**
   * Get the current ephemeris tracking status.
   * GET /api/tracking/status
   * @returns {Promise<object>} EphemerisTrackStatus
   */
  async function getTrackingStatus() {
    return request('/tracking/status');
  }

  /**
   * Start ephemeris tracking for a cached object by ID.
   * POST /api/tracking/start
   * @param {string} objectId - Object identifier
   * @param {object} [options] - Optional tracking parameters
   * @returns {Promise<object>} EphemerisTrackStatus
   */
  async function startTracking(objectId, options = {}) {
    return request('/tracking/start', {
      method: 'POST',
      body: JSON.stringify({ object_id: objectId, ...options }),
    });
  }

  /**
   * Upload ephemeris data and start tracking in one call.
   * POST /api/tracking/start-with-data
   * @param {object} trackRequest - EphemerisTrackRequest
   * @returns {Promise<object>} EphemerisTrackStatus
   */
  async function startTrackingWithData(trackRequest) {
    return request('/tracking/start-with-data', {
      method: 'POST',
      body: JSON.stringify(trackRequest),
    });
  }

  /**
   * Upload ephemeris data for a moving object.
   * POST /api/tracking/upload
   * @param {object} ephemerisData - EphemerisData
   * @returns {Promise<object>}
   */
  async function uploadEphemeris(ephemerisData) {
    return request('/tracking/upload', {
      method: 'POST',
      body: JSON.stringify(ephemerisData),
    });
  }

  /**
   * Stop the currently active ephemeris tracking.
   * POST /api/tracking/stop
   * @returns {Promise<object>}
   */
  async function stopTracking() {
    return request('/tracking/stop', { method: 'POST' });
  }

  /**
   * Get ephemeris tracking metrics.
   * GET /api/tracking/metrics
   * @returns {Promise<object>} EphemerisMetrics
   */
  async function getTrackingMetrics() {
    return request('/tracking/metrics');
  }

  /**
   * Clear cached ephemeris data.
   * POST /api/tracking/clear-cache
   * @returns {Promise<object>}
   */
  async function clearEphemerisCache() {
    return request('/tracking/clear-cache', { method: 'POST' });
  }

  // ─── Import / Export Methods ──────────────────────────────────────────

  /**
   * Import objects from uploaded file data.
   * POST /api/db/import
   * @param {string} format - Format: CSV, JSON, FITS, VOTABLE, SIMBAD, NED, MPC
   * @param {string} data - File content as string
   * @param {object} [options]
   * @param {string} [options.catalog_name] - Optional catalog name
   * @param {boolean} [options.overwrite] - Overwrite existing objects
   * @param {object} [options.field_mapping] - Column/field mapping
   * @returns {Promise<object>} ImportResult
   */
  async function importCatalog(format, data, options = {}) {
    return request('/db/import', {
      method: 'POST',
      timeout: 120000,
      body: { format, data, ...options },
    });
  }

  /**
   * Import objects from a remote URL.
   * POST /api/db/import/url
   * @param {string} url - Remote URL to fetch
   * @param {string} format - Format: CSV, JSON, FITS, VOTABLE, SIMBAD, NED, MPC
   * @param {object} [options]
   * @param {string} [options.catalog_name]
   * @param {boolean} [options.overwrite]
   * @param {object} [options.field_mapping]
   * @returns {Promise<object>} ImportResult
   */
  async function importCatalogFromUrl(url, format, options = {}) {
    return request('/db/import/url', {
      method: 'POST',
      timeout: 120000,
      body: { url, format, ...options },
    });
  }

  /**
   * Get list of well-known catalog import presets.
   * GET /api/db/import/presets
   * @returns {Promise<{presets: Array}>}
   */
  async function getImportPresets() {
    return request('/db/import/presets');
  }

  /**
   * Import a well-known catalog by preset name.
   * POST /api/db/import/preset/:name
   * @param {string} name - Preset name (e.g. 'messier', 'ngc', 'caldwell', 'hyg')
   * @param {object} [options]
   * @param {boolean} [options.overwrite]
   * @returns {Promise<object>} ImportResult
   */
  async function importPreset(name, options = {}) {
    return request(`/db/import/preset/${encodeURIComponent(name)}`, {
      method: 'POST',
      timeout: 120000,
      body: options,
    });
  }

  // ─── Field Rotation / Derotator API Methods ────────────────────────────

  /**
   * Get current derotator status.
   * GET /api/derotator/status
   * @returns {Promise<object>} DerotatorStatus
   */
  async function getDerotatorStatus() {
    return request('/derotator/status');
  }

  /**
   * Get current field rotation parameters.
   * GET /api/field-rotation/params
   * @returns {Promise<object>} FieldRotationParams
   */
  async function getFieldRotationParams() {
    return request('/field-rotation/params');
  }

  /**
   * Enable or disable field rotation compensation.
   * POST /api/field-rotation/enable
   * @param {object} params - { enabled: boolean, latitude?: number, ... }
   * @returns {Promise<object>}
   */
  async function enableFieldRotation(params) {
    return request('/field-rotation/enable', {
      method: 'POST',
      body: JSON.stringify(params),
    });
  }


  /**
   * Get current gamepad/joystick state (axes, buttons, connection).
   * GET /api/hal/gamepad/state
   * @returns {Promise<object>} GamepadState
   */
  async function getGamepadState() {
    return request('/hal/gamepad/state', { timeout: 3000 });
  }

  /**
   * Start the gamepad manual-control loop (axis velocity commands).
   * POST /api/hal/gamepad/start
   * @returns {Promise<object>}
   */
  async function startGamepad() {
    return request('/hal/gamepad/start', { method: 'POST', timeout: 5000 });
  }

  /**
   * Stop the gamepad manual-control loop.
   * POST /api/hal/gamepad/stop
   * @returns {Promise<object>}
   */
  async function stopGamepad() {
    return request('/hal/gamepad/stop', { method: 'POST', timeout: 5000 });
  }

  /**
   * Set the gamepad navigation mode.
   * POST /api/hal/gamepad/mode
   * @param {number} mode - 0=RAW, 1=CELESTIAL, 2=ALT_AZ
   * @returns {Promise<object>}
   */
  async function setGamepadMode(mode) {
    return request('/hal/gamepad/mode', { method: 'POST', body: JSON.stringify({ mode: mode }), timeout: 5000 });
  }

  // Public API
  return {
    getStatus,
    slewToCoordinates,
    slewHorizontal,
    stopMount,
    parkMount,
    unparkMount,
    clearErrors,
    saveState,
    loadState,
    uploadAndLoadState,
    moveAxis,
    moveAxisRelative,
    stopAxis,
    emergencyStop,
    getMountOrientation,
    setMountOrientation,
    getHALConfig,
    setHALConfig,
    getConfig,
    updateConfig,
    resetConfig,
    resetGroupConfig,
    getAddresses,
    setAddresses,
    checkHealth,
    // Database methods
    checkDbHealth,
    getDbStats,
    listObjects,
    searchObjects,
    getObject,
    createObject,
    updateObject,
    deleteObject,
    getFavorites,
    addFavorite,
    removeFavorite,
    listCategories,
    createCategory,
    getTonightBest,
    // Calibration methods
    getBootstrapStatus,
    addBootstrapMeasurement,
    runBootstrapCalibration,
    clearBootstrapMeasurements,
    getTPointParameters,
    addTPointMeasurement,
    runTPointCalibration,
    clearTPointMeasurements,
    // Tracking methods
    getTrackingStatus,
    startTracking,
    startTrackingWithData,
    uploadEphemeris,
    stopTracking,
    getTrackingMetrics,
    clearEphemerisCache,
    // Import methods
    importCatalog,
    importCatalogFromUrl,
    getImportPresets,
    importPreset,
    // Field Rotation / Derotator methods
    getDerotatorStatus,
    getFieldRotationParams,
    enableFieldRotation,
    // Gamepad
    getGamepadState,
    startGamepad,
    stopGamepad,
    setGamepadMode,
  };
})();
