/**
 * Health Routes — Database and system health checks
 */
'use strict';

const express = require('express');
const router = express.Router();
const { dbGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/db/health
 * Check database service health.
 */
router.get('/health', async (req, res) => {
  try {
    const result = await dbGrpcCall('HealthCheck', {}, 5);
    res.json({ status: 'healthy', ...result });
  } catch (err) {
    errorResponse(res, 503, 'Database health check failed', err.message);
  }
});

module.exports = router;
