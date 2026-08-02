/**
 * Log Routes — SSE log streaming and log retrieval
 */
'use strict';

const express = require('express');
const router = express.Router();
const { grpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * GET /api/logs/stream
 * SSE (Server-Sent Events) endpoint for real-time log streaming.
 */
router.get('/stream', (req, res) => {
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive',
  });

  const client = require('../grpc/client').getGrpcClient();

  const call = client.SubscribeToLogs({});
  call.on('data', (logEntry) => {
    res.write(`data: ${JSON.stringify(logEntry)}\n\n`);
  });
  call.on('error', (err) => {
    res.write(`data: ${JSON.stringify({ error: err.message })}\n\n`);
    res.end();
  });
  call.on('end', () => {
    res.end();
  });

  req.on('close', () => {
    call.cancel();
  });
});

/**
 * GET /api/logs
 * Get recent log entries.
 * Query params: level?, limit?, since?
 */
router.get('/', async (req, res) => {
  try {
    const { level, limit, since } = req.query;
    const result = await grpcCall('GetLogs', {
      level: level || '',
      limit: parseInt(limit, 10) || 100,
      since: since || '',
    });
    res.json(result);
  } catch (err) {
    errorResponse(res, 502, 'Failed to get logs', err.message);
  }
});

module.exports = router;
