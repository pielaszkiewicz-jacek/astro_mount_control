/**
 * Central Error Handling Middleware
 */
'use strict';

/**
 * Global error handler — catches unhandled errors from all routes.
 */
function errorHandler(err, req, res, _next) {
  console.error('[Error]', err.stack || err.message || err);
  res.status(500).json({ error: 'Internal server error' });
}

module.exports = errorHandler;
