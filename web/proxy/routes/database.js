/**
 * Database Routes — Object database CRUD operations
 */
'use strict';

const express = require('express');
const router = express.Router();
const { dbGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');
const builtinCatalog = require('../builtin-catalog');

// ─── Stats & Health ─────────────────────────────────────────────────────

/**
 * GET /api/db/stats
 * Returns database statistics and health.
 */
router.get('/stats', async (req, res) => {
  const result = await dbCallOrFallback('GetDatabaseStats', {}, 3, {
    total_objects: builtinCatalog.searchObjects({ limit: 10000 }).total_count,
    total_catalogs: 5,
    total_favorites: 0,
    status: 'builtin',
    message: 'Using built-in catalog (offline mode)'
  });
  res.json(result);
});

// ─── Objects CRUD ───────────────────────────────────────────────────────

/**
 * GET /api/db/objects
 * List / search astronomical objects with pagination.
 */
// Helper: perform a db gRPC call with a short timeout, returning fallback on failure
// For list/search RPCs (ListObjects, SearchObjects): if gRPC succeeds but returns
// an empty object list, the fallback is used instead — this ensures the built-in
// catalog always provides results even when the real database has no matching data.
// For other RPCs (GetDatabaseStats, etc.): the raw gRPC response is returned as-is.
async function dbCallOrFallback(method, request, timeoutSeconds = 3, fallback) {
  try {
    const result = await dbGrpcCall(method, request, timeoutSeconds);
    // Only apply the empty-result fallback for list/search RPCs
    if (method === 'ListObjects' || method === 'SearchObjects') {
      if (result && result.objects && result.objects.length > 0) {
        return result;
      }
      return fallback;
    }
    return result;
  } catch (err) {
    return fallback;
  }
}

router.get('/objects', async (req, res) => {
  const { name, type, limit, offset, page, pageSize, sortBy, sortDescending,
          filterType, minMagnitude, maxMagnitude } = req.query;
  const currentPage = parseInt(page, 10) || 1;
  const currentPageSize = parseInt(pageSize || limit, 10) || 20;
  const currentOffset = parseInt(offset, 10);
  const calcOffset = !isNaN(currentOffset) ? currentOffset : (currentPage - 1) * currentPageSize;
  const fallback = builtinCatalog.searchObjects({
    name: name || '',
    type: type || filterType || '',
    limit: currentPageSize,
    offset: calcOffset,
    sort_by: sortBy || 'name',
    sort_descending: sortDescending === 'true',
    min_magnitude: minMagnitude ? parseFloat(minMagnitude) : undefined,
    max_magnitude: maxMagnitude ? parseFloat(maxMagnitude) : undefined,
  });
  const result = await dbCallOrFallback('ListObjects', {
    page: currentPage,
    page_size: currentPageSize,
    sort_by: sortBy || 'name',
    sort_descending: sortDescending === 'true',
    filter_type: filterType || undefined,
    min_magnitude: minMagnitude ? parseFloat(minMagnitude) : undefined,
    max_magnitude: maxMagnitude ? parseFloat(maxMagnitude) : undefined,
  }, 3, fallback);
  res.json(result);
});

/**
 * GET /api/db/objects/search
 * Full-text search across objects with advanced filters.
 */
router.get('/objects/search', async (req, res) => {
  const { query, objectType, q, type, limit, offset,
          minMagnitude, maxMagnitude,
          favoritesOnly, visibleOnly, catalogs, constellation } = req.query;
  const searchName = query || q || '';
  const catalogsArr = catalogs ? catalogs.split(',').map(s => s.trim()).filter(Boolean) : undefined;
  const searchLimit = parseInt(limit, 10) || 50;
  const searchOffset = parseInt(offset, 10) || 0;
  const fallback = builtinCatalog.searchObjects({
    name: searchName,
    type: objectType || type || '',
    limit: searchLimit,
    offset: searchOffset,
    min_magnitude: minMagnitude ? parseFloat(minMagnitude) : undefined,
    max_magnitude: maxMagnitude ? parseFloat(maxMagnitude) : undefined,
    catalogs: catalogsArr,
    constellation: constellation || undefined,
  });
  const result = await dbCallOrFallback('SearchObjects', {
    query: searchName || undefined,
    object_type: objectType || type || undefined,
    min_magnitude: minMagnitude ? parseFloat(minMagnitude) : undefined,
    max_magnitude: maxMagnitude ? parseFloat(maxMagnitude) : undefined,
    catalogs: catalogsArr,
    include_favorites_only: favoritesOnly === 'true' || undefined,
    include_visible_only: visibleOnly === 'true' || undefined,
    constellation: constellation || undefined,
  }, 3, fallback);
  res.json(result);
});

/**
 * GET /api/db/objects/:id
 * Get a specific object by ID.
 */
router.get('/objects/:id', async (req, res) => {
  try {
    const result = await dbGrpcCall('GetObject', { id: req.params.id });
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Failed to get object', err.message);
  }
});

/**
 * POST /api/db/objects
 * Create a new astronomical object.
 * Body: AstronomicalObject fields
 */
router.post('/objects', async (req, res) => {
  try {
    const objectData = req.body;
    if (!objectData || !objectData.name) {
      return errorResponse(res, 400, 'Missing required field: name');
    }
    await dbGrpcCall('CreateObject', objectData, 30);
    res.json({ success: true, message: `Object '${objectData.name}' created` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to create object', err.message);
  }
});

/**
 * PUT /api/db/objects/:id
 * Update an existing object.
 */
router.put('/objects/:id', async (req, res) => {
  try {
    const updateData = { id: req.params.id, ...req.body };
    await dbGrpcCall('UpdateObject', updateData, 30);
    res.json({ success: true, message: `Object '${req.params.id}' updated` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to update object', err.message);
  }
});

/**
 * DELETE /api/db/objects/:id
 * Delete an object by ID.
 */
router.delete('/objects/:id', async (req, res) => {
  try {
    await dbGrpcCall('DeleteObject', { id: req.params.id });
    res.json({ success: true, message: `Object '${req.params.id}' deleted` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to delete object', err.message);
  }
});

// ─── Favorites ──────────────────────────────────────────────────────────

/**
 * POST /api/db/favorites
 * Add an object to favorites.
 * Body: { object_id: string }
 */
router.post('/favorites', async (req, res) => {
  try {
    const { object_id } = req.body;
    if (!object_id) {
      return errorResponse(res, 400, 'Missing required field: object_id');
    }
    await dbGrpcCall('AddFavorite', { object_id });
    res.json({ success: true, message: `Added ${object_id} to favorites` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to add favorite', err.message);
  }
});

/**
 * GET /api/db/favorites
 * List all favorite objects.
 */
router.get('/favorites', async (req, res) => {
  try {
    const result = await dbGrpcCall('GetFavorites', {});
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Failed to get favorites', err.message);
  }
});

/**
 * DELETE /api/db/favorites/:id
 * Remove an object from favorites.
 */
router.delete('/favorites/:id', async (req, res) => {
  try {
    await dbGrpcCall('RemoveFavorite', { object_id: req.params.id });
    res.json({ success: true, message: `Removed ${req.params.id} from favorites` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to remove favorite', err.message);
  }
});

// ─── Categories ─────────────────────────────────────────────────────────

/**
 * GET /api/db/categories
 * List all categories.
 */
router.get('/categories', async (req, res) => {
  try {
    const result = await dbGrpcCall('ListCategories', {});
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Failed to list categories', err.message);
  }
});

/**
 * POST /api/db/categories
 * Create a new category.
 * Body: { name, description?, color?, icon? }
 */
router.post('/categories', async (req, res) => {
  try {
    const catData = req.body;
    if (!catData || !catData.name) {
      return errorResponse(res, 400, 'Missing required field: name');
    }
    await dbGrpcCall('CreateCategory', catData);
    res.json({ success: true, message: `Category '${catData.name}' created` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to create category', err.message);
  }
});

// ─── Tonight's Best ─────────────────────────────────────────────────────

/**
 * GET /api/db/tonight
 * Get tonight's best observable objects.
 */
router.get('/tonight', async (req, res) => {
  try {
    const { latitude, longitude, altitude, maxResults, minMagnitude, maxMagnitude } = req.query;
    const result = await dbGrpcCall('GetTonightBest', {
      latitude: latitude ? parseFloat(latitude) : undefined,
      longitude: longitude ? parseFloat(longitude) : undefined,
      altitude: altitude ? parseFloat(altitude) : undefined,
      max_results: maxResults ? parseInt(maxResults, 10) : undefined,
      min_magnitude: minMagnitude ? parseFloat(minMagnitude) : undefined,
      max_magnitude: maxMagnitude ? parseFloat(maxMagnitude) : undefined,
    }, 30);
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Failed to get tonight\'s best', err.message);
  }
});

// ─── Import / Export ────────────────────────────────────────────────────

/**
 * POST /api/db/import
 * Import objects from uploaded file data.
 * Body: { format, data, catalog_name?, overwrite?, field_mapping? }
 */
router.post('/import', async (req, res) => {
  try {
    const { format, data, catalog_name, overwrite, field_mapping } = req.body;
    if (!format || !data) {
      return errorResponse(res, 400, 'Missing required fields: format, data');
    }
    // Convert format name to enum value — backend only supports CSV (0)
    const FORMAT_ENUM_LOCAL = { CSV: 0, JSON: 1, FITS: 2, VOTABLE: 3, SIMBAD: 4, NED: 5, MPC: 6 };
    const formatCode = FORMAT_ENUM_LOCAL[format] !== undefined ? FORMAT_ENUM_LOCAL[format] : 0;
    const result = await dbGrpcCall('ImportCatalog', {
      format: formatCode,
      data: data,
      catalog_name: catalog_name || '',
      overwrite: !!overwrite,
      field_mapping: field_mapping || {},
    }, 300); // 5 min timeout for large imports
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Import failed', err.message);
  }
});

/**
 * POST /api/db/import/url
 * Import objects from a remote URL.
 * The proxy fetches the URL and passes the data as bytes to the gRPC ImportCatalog.
 * Body: { url, format, catalog_name?, overwrite?, field_mapping? }
 */
router.post('/import/url', async (req, res) => {
  try {
    const { url, format, catalog_name, overwrite, field_mapping } = req.body;
    if (!url || !format) {
      return errorResponse(res, 400, 'Missing required fields: url, format');
    }

    // Fetch the catalog data from the remote URL on the proxy side
    const https = require('https');
    const http = require('http');

    const response = await new Promise((resolve, reject) => {
      const client = url.startsWith('https') ? https : http;
      client.get(url, (resp) => {
        if (resp.statusCode < 200 || resp.statusCode >= 300) {
          reject(new Error(`HTTP ${resp.statusCode} fetching ${url}`));
          return;
        }
        const chunks = [];
        resp.on('data', (chunk) => chunks.push(chunk));
        resp.on('end', () => resolve(Buffer.concat(chunks)));
        resp.on('error', reject);
      }).on('error', reject);
    });

    const formatEnum = { CSV: 0, JSON: 1, FITS: 2, VOTABLE: 3, SIMBAD: 4, NED: 5, MPC: 6 };

    const result = await dbGrpcCall('ImportCatalog', {
      format: formatEnum[format] !== undefined ? formatEnum[format] : 0,
      data: response,
      catalog_name: catalog_name || 'Imported Catalog',
      overwrite: !!overwrite,
      field_mapping: field_mapping || {},
    }, 300);

    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Import from URL failed', err.message);
  }
});

/**
 * GET /api/db/import/presets
 * List well-known catalog import presets.
 *
 * These are hardcoded here because the database backend does not
 * expose a GetImportPresets RPC.  Each preset defines a well-known
 * catalog that the frontend can import by name via POST /import/preset/:name,
 * which in turn calls gRPC ImportCatalog with the preset's URL / file path.
 */
// Field mappings define how CSV columns map to protobuf AstronomicalObject fields.
// The key is the proto field name, the value is the CSV column header (case-sensitive).
// Supported proto field keys: name, ra, dec, type, magnitude, b_magnitude, spectral_type,
// catalog_id, catalog_name, constellation, angular_size, radial_velocity, redshift, distance, notes
const CATALOG_FIELD_MAPPINGS = {
  // OpenNGC format (used by Messier, NGC, IC):
  // Name;Type;RA;Dec;Const;MajAx;MinAx;PosAng;B-Mag;V-Mag;J-Mag;H-Mag;K-Mag;SurfBr;Hubble;Pax;Pm-RA;Pm-Dec;RadVel;Redshift;...
  messier: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'type': 'Type',
    'magnitude': 'V-Mag',
    'b_magnitude': 'B-Mag',
    'constellation': 'Const',
    'angular_size': 'MajAx',
    'radial_velocity': 'RadVel',
    'redshift': 'Redshift',
    'catalog_id': 'M',
    'notes': 'Common names',
  },
  ngc: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'type': 'Type',
    'magnitude': 'V-Mag',
    'b_magnitude': 'B-Mag',
    'constellation': 'Const',
    'angular_size': 'MajAx',
    'radial_velocity': 'RadVel',
    'redshift': 'Redshift',
    'catalog_id': 'NGC',
    'notes': 'Common names',
  },
  ic: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'type': 'Type',
    'magnitude': 'V-Mag',
    'b_magnitude': 'B-Mag',
    'constellation': 'Const',
    'angular_size': 'MajAx',
    'radial_velocity': 'RadVel',
    'redshift': 'Redshift',
    'catalog_id': 'IC',
    'notes': 'Common names',
  },
  caldwell: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'type': 'Type',
    'magnitude': 'V-Mag',
    'b_magnitude': 'B-Mag',
    'constellation': 'Const',
    'angular_size': 'MajAx',
    'notes': 'Common names',
  },
  // HYG Database (cleaned format):
  // Name,RA,Dec,V-Mag,Spectrum,Dist,HIP
  // Name uses proper name, Bayer/Flamsteed, or HIP ID as fallback
  // Note: HYG 'ra' is in decimal degrees, 'dist' in parsecs, 'mag' is V magnitude
  hyg: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'magnitude': 'V-Mag',
    'spectral_type': 'Spectrum',
    'distance': 'Dist',
  },
  sao: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'magnitude': 'V-Mag',
  },
  hipparcos: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'magnitude': 'V-Mag',
    'spectral_type': 'Spectrum',
  },
  bright_stars: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'magnitude': 'V-Mag',
    'spectral_type': 'Spectrum',
  },
  double_stars: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'magnitude': 'V-Mag',
  },
  solar_system: {
    'name': 'Name',
    'ra': 'RA',
    'dec': 'Dec',
    'magnitude': 'V-Mag',
    'type': 'Type',
  },
};

const CATALOG_PRESETS = [
  { name: 'messier',  label: 'Messier Catalog',       description: '107 deep-sky objects catalogued by Charles Messier (from OpenNGC)',                  format: 'CSV',  type: 'Deep Sky',  size: 107 },
  { name: 'ngc',      label: 'NGC Catalog',           description: 'New General Catalogue — 13,969 deep-sky objects (from OpenNGC)',                     format: 'CSV',  type: 'Deep Sky',  size: 13969 },
  { name: 'ic',       label: 'IC Catalog',            description: 'Index Catalogue — 5,596 supplementary objects (from OpenNGC)',                       format: 'CSV',  type: 'Deep Sky',  size: 5596 },
  { name: 'caldwell', label: 'Caldwell Catalog',      description: '106 star clusters, nebulae and galaxies compiled by Patrick Moore (from OpenNGC)',    format: 'CSV',  type: 'Deep Sky',  size: 106 },
  { name: 'hyg',      label: 'HYG Database',          description: 'Hipparcos-2 + Yale Bright Star + Gliese — 117,951 stars with proper motion',         format: 'CSV',  type: 'Stars',     size: 117951 },
  { name: 'sao',      label: 'SAO Star Catalog',      description: 'Smithsonian Astrophysical Observatory — 117,951 stars (Hipparcos cross-match)',       format: 'CSV',  type: 'Stars',     size: 117951 },
  { name: 'hipparcos',label: 'Hipparcos Catalog',     description: 'ESA Hipparcos mission — 117,951 stars with high-precision astrometry',                format: 'CSV',  type: 'Stars',     size: 117951 },
  { name: 'bright_stars', label: 'Bright Star Catalog',description: 'Yale Bright Star Catalog — 8,870 stars brighter than magnitude 6.5',                 format: 'CSV',  type: 'Stars',     size: 8870 },
  { name: 'double_stars', label: 'Double Stars',      description: '50,000 star systems with Bayer/Flamsteed designations (from HYG)',                    format: 'CSV',  type: 'Stars',     size: 50000 },
  { name: 'solar_system', label: 'Solar System Bodies',description: 'Major planets, moons, bright asteroids and periodic comets',                        format: 'CSV',  type: 'Solar System', size: 29 },
];

router.get('/import/presets', (req, res) => {
  res.json({ presets: CATALOG_PRESETS });
});

/**
 * POST /api/db/import/preset/:name
 * Import a well-known catalog by preset name.
 *
 * Looks up the preset in CATALOG_PRESETS, fetches the catalog data
 * from a configured data directory, and passes it to the gRPC ImportCatalog.
 *
 * The catalog data files must be placed in the data/ directory
 * (configured via CATALOG_DIR or CATALOG_URLS).
 */
const https = require('https');
const http = require('http');
const fs = require('fs');
const path = require('path');

// Local data directory for catalog files (override via env CATALOG_DATA_DIR)
const CATALOG_DATA_DIR = process.env.CATALOG_DATA_DIR || path.join(__dirname, '../../data/catalogs');

// Fallback remote URLs (only used if local file not found)
const CATALOG_URLS = {};

const FORMAT_ENUM = { CSV: 0, JSON: 1, FITS: 2, VOTABLE: 3, SIMBAD: 4, NED: 5, MPC: 6 };

function fetchUrl(url) {
  return new Promise((resolve, reject) => {
    const client = url.startsWith('https') ? https : http;
    client.get(url, (resp) => {
      if (resp.statusCode < 200 || resp.statusCode >= 300) {
        reject(new Error(`HTTP ${resp.statusCode} fetching ${url}`));
        return;
      }
      const chunks = [];
      resp.on('data', (chunk) => chunks.push(chunk));
      resp.on('end', () => resolve(Buffer.concat(chunks)));
      resp.on('error', reject);
    }).on('error', reject);
  });
}

router.post('/import/preset/:name', async (req, res) => {
  try {
    const presetName = req.params.name;
    const preset = CATALOG_PRESETS.find(p => p.name === presetName);
    if (!preset) {
      return errorResponse(res, 404, `Unknown preset: ${presetName}`);
    }

    const { overwrite } = req.body;

    // Try local file matching the preset's declared format first
    const fileExt = (preset.format || 'CSV').toLowerCase();
    const localFile = path.join(CATALOG_DATA_DIR, presetName + '.' + fileExt);
    let data = null;

    if (fs.existsSync(localFile)) {
      data = fs.readFileSync(localFile);
      console.log(`[Import] Reading preset '${presetName}' from local file: ${localFile}`);
    } else {
      // Fall back to remote URL if configured
      const url = CATALOG_URLS[presetName];
      if (url) {
        console.log(`[Import] Fetching preset '${presetName}' from remote URL: ${url}`);
        data = await fetchUrl(url);
      }
    }

    if (!data) {
      return errorResponse(res, 404,
        `No data source for preset '${presetName}'. ` +
        `Place ${presetName}.csv in ${CATALOG_DATA_DIR}/, ` +
        `or use File Import or URL Import instead.`);
    }

    // Use catalog-specific field mapping; fall back to empty map (backend skips unmapped rows)
    const field_mapping = CATALOG_FIELD_MAPPINGS[presetName] || {};

    const result = await dbGrpcCall('ImportCatalog', {
      format: FORMAT_ENUM[preset.format] !== undefined ? FORMAT_ENUM[preset.format] : 0,
      data: data,
      catalog_name: preset.label,
      overwrite: !!overwrite,
      field_mapping: field_mapping,
    }, 300);

    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Failed to import preset', err.message);
  }
});

module.exports = router;
