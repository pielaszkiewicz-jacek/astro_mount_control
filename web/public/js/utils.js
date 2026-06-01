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
   * Format RA in hours to a readable string.
   * @param {number} hours
   * @returns {string}
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
   * Format Dec in degrees to a readable string.
   * @param {number} degrees
   * @returns {string}
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
    formatTime,
    clamp,
    escapeHtml,
    EventBus,
    $,
    $$,
  };
})();
