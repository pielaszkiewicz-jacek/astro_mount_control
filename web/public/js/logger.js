/**
 * Astronomical Mount Controller - Application Logger
 *
 * Captures console output (log, warn, error, info, debug) into an internal
 * ring buffer for viewing in the UI log panel.
 */
const Logger = (() => {
  'use strict';

  const MAX_LOGS = 1000;
  let logs = [];
  let originalMethods = {};

  /**
   * Initialize the logger: wrap console methods to capture all output.
   */
  function init() {
    originalMethods.log = console.log;
    originalMethods.warn = console.warn;
    originalMethods.error = console.error;
    originalMethods.info = console.info;
    originalMethods.debug = console.debug;

    console.log = (...args) => { addEntry('info', args); originalMethods.log.apply(console, args); };
    console.warn = (...args) => { addEntry('warn', args); originalMethods.warn.apply(console, args); };
    console.error = (...args) => { addEntry('error', args); originalMethods.error.apply(console, args); };
    console.info = (...args) => { addEntry('info', args); originalMethods.info.apply(console, args); };
    console.debug = (...args) => { addEntry('debug', args); originalMethods.debug.apply(console, args); };
  }

  /**
   * Serialize an argument to a readable string.
   */
  function formatArg(arg) {
    if (arg === null) return 'null';
    if (arg === undefined) return 'undefined';
    if (typeof arg === 'string') return arg;
    if (typeof arg === 'object') {
      try { return JSON.stringify(arg); } catch (_) { return String(arg); }
    }
    return String(arg);
  }

  /**
   * Add a log entry to the ring buffer.
   */
  function addEntry(level, args) {
    const message = args.map(formatArg).join(' ');
    logs.push({
      timestamp: new Date(),
      level: level,
      message: message,
    });
    if (logs.length > MAX_LOGS) logs.shift();
  }

  /**
   * Get all stored log entries.
   * @returns {Array<{timestamp: Date, level: string, message: string}>}
   */
  function getLogs() {
    return logs;
  }

  /**
   * Clear all stored log entries.
   */
  function clearLogs() {
    logs = [];
  }

  /**
   * Format a timestamp to HH:MM:SS.
   */
  function formatTime(ts) {
    return ts.toLocaleTimeString('en-US', { hour12: false });
  }

  /**
   * Generate HTML for all log entries.
   * @returns {string} HTML string
   */
  function getLogHtml() {
    return logs.map((entry) => {
      const time = formatTime(entry.timestamp);
      const safeMsg = escapeHtml(entry.message);
      return `<div class="log-entry log-${entry.level}"><span class="log-time">${time}</span><span class="log-level">${entry.level}</span><span class="log-msg">${safeMsg}</span></div>`;
    }).join('');
  }

  /**
   * Simple HTML entity escaping.
   */
  function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
  }

  return { init, getLogs, clearLogs, getLogHtml };
})();
