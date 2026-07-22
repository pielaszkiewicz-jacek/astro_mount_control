/**
 * Database Routes — Object database CRUD operations
 */
'use strict';

const express = require('express');
const router = express.Router();
const { dbGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/db/objects
 * Search for astronomical objects.
 */
router.get('/objects', async (req, res) => {
  try {
    const { name, type, limit, offset } = req.query;
    const result = await dbGrpcCall('SearchObjects', {
      name: name || '',
      type: type || '',
      limit: parseInt(limit, 10) || 50,
      offset: parseInt(offset, 10) || 0,
    }, 30);
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Database query failed', err.message);
  }
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

module.exports = router;
