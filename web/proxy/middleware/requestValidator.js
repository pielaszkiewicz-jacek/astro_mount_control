/**
 * Request Validation Middleware
 */
'use strict';

/**
 * Validates that required fields are present in the request body.
 * @param {string[]} fields - List of required field names
 */
function requireFields(fields) {
  return (req, res, next) => {
    const missing = fields.filter(f => req.body[f] === undefined);
    if (missing.length > 0) {
      return res.status(400).json({
        error: `Missing required fields: ${missing.join(', ')}`,
      });
    }
    next();
  };
}

/**
 * Validates that a field is a number in a given range.
 * @param {string} field - Field name
 * @param {number} min - Minimum value
 * @param {number} max - Maximum value
 */
function validateNumberRange(field, min, max) {
  return (req, res, next) => {
    const val = req.body[field];
    if (val !== undefined && (typeof val !== 'number' || val < min || val > max)) {
      return res.status(400).json({
        error: `${field} must be a number in range [${min}, ${max}]`,
      });
    }
    next();
  };
}

module.exports = { requireFields, validateNumberRange };
