/**
 * State Routes — Save/Load mount state
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * POST /api/state/save
 * Save current mount state.
 * Body: { filename?: string }
 */
router.post('/save', async (req, res) => {
  try {
    const { filename } = req.body;
    const result = await grpcCall('SaveState', { filename: filename || '' });
    res.json({ success: true, path: result.path, message: 'State saved' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to save state', err.message);
  }
});

/**
 * POST /api/state/load
 * Load mount state from file.
 * Body: { filename: string }
 */
router.post('/load', async (req, res) => {
  try {
    const { filename } = req.body;
    if (!filename) {
      return errorResponse(res, 400, 'Missing required field: filename');
    }
    await grpcCall('LoadState', { filename });
    res.json({ success: true, message: `State loaded from ${filename}` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to load state', err.message);
  }
});

module.exports = router;
