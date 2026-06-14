/**
 * Astronomical Mount Controller - Web Interface Utilities
 *
 * Shared utility functions used across the application.
 */

const Utils = (() => {
  'use strict';

  /**
   * Format a number to a fixed number of decimal places.
   * @param {number} value
   * @param {number} decimals
   * @returns {string}
   */
  function formatNumber(value, decimals = 2) {
    if (value === null || value === undefined || !isFinite(value)) {
      return '—';
    }
    return Number(value).toFixed(decimals);
  }

  /**
   * Format RA in hours to a readable HMS string.
   * @param {number} hours - Right Ascension in decimal hours
   * @returns {string} e.g. "12h 30m 45.1s"
   */
  function formatRA(hours) {
    if (hours === null || hours === undefined || !isFinite(hours)) {
      return '—';
    }
    const h = Math.floor(hours);
    const m = Math.floor((hours - h) * 60);
    const s = ((hours - h - m / 60) * 3600).toFixed(1);
    return `${h}h ${m}m ${s}s`;
  }

  /**
   * Format Dec in degrees to a readable DMS string.
   * @param {number} degrees - Declination in decimal degrees
   * @returns {string} e.g. "+41° 16' 12.3\""
   */
  function formatDec(degrees) {
    if (degrees === null || degrees === undefined || !isFinite(degrees)) {
      return '—';
    }
    const sign = degrees >= 0 ? '+' : '';
    const d = Math.floor(Math.abs(degrees));
    const m = Math.floor((Math.abs(degrees) - d) * 60);
    const s = ((Math.abs(degrees) - d - m / 60) * 3600).toFixed(1);
    return `${sign}${d}° ${m}' ${s}"`;
  }

  /**
   * Format an angle in decimal degrees to a DMS string.
   * For RA display, use formatRA() instead.
   * @param {number} degrees - Angle in decimal degrees
   * @param {boolean} [showSign=true] - Whether to show +/- sign
   * @returns {string} e.g. "152° 33' 00.0\""
   */
  function formatAngleDeg(degrees, showSign = true) {
    if (degrees === null || degrees === undefined || !isFinite(degrees)) {
      return '—';
    }
    const sign = (showSign && degrees < 0) ? '-' : '';
    const d = Math.floor(Math.abs(degrees));
    const m = Math.floor((Math.abs(degrees) - d) * 60);
    const s = ((Math.abs(degrees) - d - m / 60) * 3600).toFixed(1);
    return `${sign}${d}° ${m}' ${s}"`;
  }

  /**
   * Format an angle in decimal hours to an HMS string.
   * @param {number} hours - Angle in decimal hours
   * @returns {string} e.g. "10h 30m 45.1s"
   */
  function formatAngleHours(hours) {
    return formatRA(hours);
  }

  /**
   * Parse a DMS (degrees/minutes/seconds) string into decimal degrees.
   * Supports formats:
   *   "12°34'56.7\"", "12 34 56.7", "12:34:56.7",
   *   "-12°34'56.7\"", "12d34m56.7s", "12.5824"
   * @param {string} str - The DMS string to parse
   * @returns {number} Decimal degrees, or NaN if invalid
   */
  function parseDMS(str) {
    if (!str || typeof str !== 'string') return NaN;
    str = str.trim();
    if (str === '') return NaN;

    // Try plain decimal number first
    const plainNum = parseFloat(str);
    if (!isNaN(plainNum) && String(plainNum) === str) {
      return plainNum;
    }

    // Remove degree/minute/second symbols for unified parsing
    let cleaned = str
      .replace(/[d°]/gi, ' ')
      .replace(/[''′m]/g, ' ')
      .replace(/[""″s]/gi, ' ')
      .replace(/[:;,]/g, ' ')
      .replace(/\s+/g, ' ')
      .trim();

    // Detect negative sign
    let sign = 1;
    if (cleaned.startsWith('-')) {
      sign = -1;
      cleaned = cleaned.substring(1).trim();
    }

    const parts = cleaned.split(' ');
    if (parts.length < 1 || parts.length > 3) {
      // Try parsing with regex for more flexible formats
      const match = str.match(/^([+-]?)\s*(\d+(?:\.\d+)?)\s*[°d]?\s*(\d+(?:\.\d+)?)?\s*['′m]?\s*(\d+(?:\.\d+)?)?\s*["″s]?$/i);
      if (match) {
        const s = (match[1] === '-') ? -1 : 1;
        const deg = parseFloat(match[2]) || 0;
        const min = parseFloat(match[3]) || 0;
        const sec = parseFloat(match[4]) || 0;
        return s * (deg + min / 60 + sec / 3600);
      }
      return NaN;
    }

    const deg = parseFloat(parts[0]) || 0;
    const min = parts.length >= 2 ? (parseFloat(parts[1]) || 0) : 0;
    const sec = parts.length >= 3 ? (parseFloat(parts[2]) || 0) : 0;

    return sign * (deg + min / 60 + sec / 3600);
  }

  /**
   * Parse an HMS (hours/minutes/seconds) string into decimal hours.
   * Supports formats:
   *   "12h30m45.1s", "12 30 45.1", "12:30:45.1",
   *   "12h 30m 45.1s", "12.5125"
   * @param {string} str - The HMS string to parse
   * @returns {number} Decimal hours, or NaN if invalid
   */
  function parseHMS(str) {
    if (!str || typeof str !== 'string') return NaN;
    str = str.trim();
    if (str === '') return NaN;

    // Try plain decimal number first
    const plainNum = parseFloat(str);
    if (!isNaN(plainNum) && String(plainNum) === str) {
      return plainNum;
    }

    // Remove hour/minute/second symbols for unified parsing
    let cleaned = str
      .replace(/h/gi, ' ')
      .replace(/m/gi, ' ')
      .replace(/s/gi, ' ')
      .replace(/[:;,]/g, ' ')
      .replace(/\s+/g, ' ')
      .trim();

    const parts = cleaned.split(' ');
    if (parts.length < 1 || parts.length > 3) {
      // Try regex for flexible formats
      const match = str.match(/^(\d+(?:\.\d+)?)\s*h?\s*(\d+(?:\.\d+)?)?\s*m?\s*(\d+(?:\.\d+)?)?\s*s?\s*$/i);
      if (match) {
        const h = parseFloat(match[1]) || 0;
        const m = parseFloat(match[2]) || 0;
        const s = parseFloat(match[3]) || 0;
        return h + m / 60 + s / 3600;
      }
      return NaN;
    }

    const h = parseFloat(parts[0]) || 0;
    const m = parts.length >= 2 ? (parseFloat(parts[1]) || 0) : 0;
    const s = parts.length >= 3 ? (parseFloat(parts[2]) || 0) : 0;

    return h + m / 60 + s / 3600;
  }

  /**
   * Create an enhanced angle input that accepts DMS/HMS format.
   * The input stores decimal value internally but displays formatted.
   *
   * Usage:
   *   Utils.enhanceAngleInput(element, { type: 'ra' });   // RA (hours)
   *   Utils.enhanceAngleInput(element, { type: 'dec' });  // Dec (degrees)
   *   Utils.enhanceAngleInput(element, { type: 'deg' });  // Generic angle (degrees)
   *
   * @param {HTMLInputElement} input - The input element to enhance
   * @param {object} options
   * @param {'ra'|'dec'|'deg'|'hours'} options.type - Angle type
   * @param {number} [options.decimals=4] - Decimal places when editing
   */
  function enhanceAngleInput(input, options = {}) {
    if (!input) return;

    const type = options.type || 'deg';

    // Store the raw decimal value as a data attribute
    function getDecimal() {
      const raw = input.getAttribute('data-angle-decimal');
      if (raw !== null) return parseFloat(raw);
      // Fallback: parse from current value (accepts DMS, HMS, or decimal)
      if (type === 'ra' || type === 'hours') {
        return parseHMS(input.value);
      }
      return parseDMS(input.value);
    }

    function setDecimal(val) {
      if (!isFinite(val)) return;
      input.setAttribute('data-angle-decimal', String(val));
      updateDisplay();
    }

    function updateDisplay() {
      const val = getDecimal();
      if (!isFinite(val)) return;
      let formatted;
      switch (type) {
        case 'ra':   formatted = formatRA(val); break;
        case 'hours': formatted = formatAngleHours(val); break;
        case 'dec':  formatted = formatDec(val); break;
        default:     formatted = formatAngleDeg(val, false); break;
      }
      input.value = formatted;
    }

    // Parse whatever the user typed and reformat on blur.
    // Accepts DMS, HMS, or plain decimal — the parser handles all.
    input.addEventListener('blur', () => {
      let parsed;
      if (type === 'ra' || type === 'hours') {
        parsed = parseHMS(input.value);
      } else {
        parsed = parseDMS(input.value);
      }
      if (isFinite(parsed)) {
        setDecimal(parsed);
      } else {
        // If parsing failed, restore from stored decimal
        updateDisplay();
      }
    });

    // When user presses Enter, parse and reformat
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        let parsed;
        if (type === 'ra' || type === 'hours') {
          parsed = parseHMS(input.value);
        } else {
          parsed = parseDMS(input.value);
        }
        if (isFinite(parsed)) {
          setDecimal(parsed);
        } else {
          updateDisplay();
        }
        input.blur();
      }
    });

    // Initialize with current value
    const initVal = parseFloat(input.value);
    if (isFinite(initVal) && initVal !== 0) {
      setDecimal(initVal);
    } else {
      // Check if there's a stored attribute
      const stored = input.getAttribute('data-angle-decimal');
      if (stored !== null) {
        setDecimal(parseFloat(stored));
      }
    }

    // Expose getter/setter on the element
    input.getAngleDecimal = getDecimal;
    input.setAngleDecimal = setDecimal;

    return input;
  }

  /**
   * Scan a container for inputs with data-angle-type attribute and enhance them.
   * @param {Element} [container=document] - Container to scan
   */
  function enhanceAllAngleInputs(container = document) {
    container.querySelectorAll('input[data-angle-type]').forEach(input => {
      // Skip already-enhanced inputs (idempotent)
      if (input.getAttribute('data-angle-enhanced') === '1') return;
      input.setAttribute('data-angle-enhanced', '1');
      const type = input.getAttribute('data-angle-type');
      const decimals = parseInt(input.getAttribute('data-angle-decimals') || '4', 10);
      enhanceAngleInput(input, { type, decimals });
    });
  }

  /**
   * Format a timestamp to a localized time string.
   * @param {Date|number|string} timestamp
   * @returns {string}
   */
  function formatTime(timestamp) {
    if (!timestamp) return '—';
    const date = timestamp instanceof Date ? timestamp : new Date(timestamp);
    return date.toLocaleTimeString('en-US', {
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
    });
  }

  /**
   * Clamp a value between min and max.
   * @param {number} value
   * @param {number} min
   * @param {number} max
   * @returns {number}
   */
  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }

  /**
   * Simple event emitter for component communication.
   */
  class EventBus {
    constructor() {
      this._listeners = {};
    }

    on(event, callback) {
      if (!this._listeners[event]) {
        this._listeners[event] = [];
      }
      this._listeners[event].push(callback);
      return () => this.off(event, callback);
    }

    off(event, callback) {
      if (!this._listeners[event]) return;
      this._listeners[event] = this._listeners[event].filter(cb => cb !== callback);
    }

    emit(event, data) {
      if (!this._listeners[event]) return;
      this._listeners[event].forEach(cb => cb(data));
    }
  }

  /**
   * Safe querySelector wrapper.
   * @param {string} selector
   * @param {Element} [context=document]
   * @returns {Element|null}
   */
  function $(selector, context = document) {
    return context.querySelector(selector);
  }

  /**
   * Safe querySelectorAll wrapper.
   * @param {string} selector
   * @param {Element} [context=document]
   * @returns {NodeList}
   */
  function $$(selector, context = document) {
    return context.querySelectorAll(selector);
  }

  /**
   * Escape HTML special characters to prevent XSS.
   * @param {string} str
   * @returns {string}
   */
  function escapeHtml(str) {
    if (str === null || str === undefined) return '';
    const div = document.createElement('div');
    div.appendChild(document.createTextNode(String(str)));
    return div.innerHTML;
  }

  // Public API
  return {
    formatNumber,
    formatRA,
    formatDec,
    formatAngleDeg,
    formatAngleHours,
    parseDMS,
    parseHMS,
    enhanceAngleInput,
    enhanceAllAngleInputs,
    formatTime,
    clamp,
    escapeHtml,
    EventBus,
    $,
    $$,
  };
})();
