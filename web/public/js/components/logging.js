/**
 * Astronomical Mount Controller - Logging Component
 *
 * Displays real-time controller logs (streamed via SSE) alongside
 * browser-side application logs from the Logger module.
 *
 * Features:
 * - Real-time log streaming from the mount controller
 * - Browser-side application log display
 * - Filter by log level (ALL, DEBUG, INFO, WARN, ERROR)
 * - Search/filter text
 * - Auto-scroll toggle
 * - Clear logs
 * - Log count indicator
 */
const LoggingComponent = (() => {
  'use strict';

  const { $, escapeHtml } = Utils;

  // ─── State ──────────────────────────────────────────────────────────────
  let eventSource = null;
  let controllerLogs = [];
  let filterLevel = 'ALL';
  let filterText = '';
  let autoScroll = true;
  let isActive = false;
  const MAX_CONTROLLER_LOGS = 5000;

  // ─── Initialization ─────────────────────────────────────────────────────

  function init() {
    console.log('[LoggingComponent] init() called');
    bindEvents();
    renderBrowserLogs();
  }

  function bindEvents() {
    const levelFilter = $('#log-filter-level');
    const textFilter = $('#log-filter-text');
    const clearBtn = $('#log-clear-controller');
    const clearAppBtn = $('#log-clear-app');
    const toggleScrollBtn = $('#log-toggle-scroll');
    const reloadBtn = $('#log-reload');

    if (levelFilter) {
      levelFilter.addEventListener('change', () => {
        filterLevel = levelFilter.value;
        renderControllerLogs();
      });
    }

    if (textFilter) {
      textFilter.addEventListener('input', () => {
        filterText = textFilter.value.toLowerCase();
        renderControllerLogs();
      });
    }

    if (clearBtn) {
      clearBtn.addEventListener('click', () => {
        controllerLogs = [];
        renderControllerLogs();
      });
    }

    if (clearAppBtn) {
      clearAppBtn.addEventListener('click', () => {
        Logger.clearLogs();
        renderBrowserLogs();
      });
    }

    if (toggleScrollBtn) {
      toggleScrollBtn.addEventListener('click', () => {
        autoScroll = !autoScroll;
        toggleScrollBtn.classList.toggle('active', autoScroll);
        toggleScrollBtn.textContent = autoScroll ? '📜 Auto-scroll ON' : '📜 Auto-scroll OFF';
        if (autoScroll) scrollToBottom('#log-controller-content');
      });
    }

    if (reloadBtn) {
      reloadBtn.addEventListener('click', () => {
        reloadControllerLogs();
      });
    }
  }

  // ─── Controller Logs (SSE) ──────────────────────────────────────────────

  /**
   * Start streaming controller logs via SSE.
   */
  function startStreaming() {
    if (eventSource) return;
    isActive = true;

    const url = `/api/logs/stream`;
    eventSource = new EventSource(url);

    eventSource.addEventListener('init', (e) => {
      try {
        const data = JSON.parse(e.data);
        if (data.logs && Array.isArray(data.logs)) {
          controllerLogs = data.logs;
          renderControllerLogs();
          updateLogCount();
        }
      } catch (err) {
        console.warn('[LoggingComponent] Failed to parse SSE init:', err);
      }
    });

    eventSource.addEventListener('log', (e) => {
      try {
        const data = JSON.parse(e.data);
        if (data && Array.isArray(data)) {
          data.forEach(entry => {
            controllerLogs.push(entry);
          });
          if (controllerLogs.length > MAX_CONTROLLER_LOGS) {
            controllerLogs = controllerLogs.slice(-MAX_CONTROLLER_LOGS);
          }
          renderControllerLogs();
          updateLogCount();
        }
      } catch (err) {
        console.warn('[LoggingComponent] Failed to parse SSE log:', err);
      }
    });

    eventSource.onerror = () => {
      console.warn('[LoggingComponent] SSE connection error, will retry...');
    };
  }

  /**
   * Stop streaming controller logs.
   */
  function stopStreaming() {
    isActive = false;
    if (eventSource) {
      eventSource.close();
      eventSource = null;
    }
  }

  /**
   * Reload controller logs from the REST endpoint.
   */
  async function reloadControllerLogs() {
    try {
      const response = await fetch('/api/logs?lines=500');
      const data = await response.json();
      if (data.logs && Array.isArray(data.logs)) {
        controllerLogs = data.logs;
        renderControllerLogs();
        updateLogCount();
      }
    } catch (err) {
      console.warn('[LoggingComponent] Failed to reload logs:', err);
    }
  }

  // ─── Rendering ──────────────────────────────────────────────────────────

  /**
   * Render controller log entries with current filters.
   */
  function renderControllerLogs() {
    const container = $('#log-controller-content');
    if (!container) return;

    const filtered = controllerLogs.filter(entry => {
      // Level filter
      if (filterLevel !== 'ALL' && entry.level !== filterLevel.toLowerCase()) {
        return false;
      }
      // Text filter
      if (filterText && !entry.message.toLowerCase().includes(filterText) &&
          !(entry.timestamp && entry.timestamp.toLowerCase().includes(filterText))) {
        return false;
      }
      return true;
    });

    if (filtered.length === 0) {
      container.innerHTML = '<div class="log-empty">No log entries matching filters.</div>';
      return;
    }

    const html = filtered.map(entry => {
      const time = entry.timestamp || '—';
      const level = entry.level || 'info';
      const msg = escapeHtml(entry.message);
      return `<div class="log-entry log-${level}">
        <span class="log-time">${escapeHtml(time)}</span>
        <span class="log-level">${escapeHtml(level)}</span>
        <span class="log-msg">${msg}</span>
      </div>`;
    }).join('');

    container.innerHTML = html;

    if (autoScroll) {
      scrollToBottom('#log-controller-content');
    }
  }

  /**
   * Render browser-side application logs.
   */
  function renderBrowserLogs() {
    const container = $('#log-app-content');
    if (!container) return;

    container.innerHTML = Logger.getLogHtml();
    scrollToBottom('#log-app-content');
  }

  /**
   * Update the log count badge.
   */
  function updateLogCount() {
    const badge = $('#log-count');
    if (badge) {
      badge.textContent = `${controllerLogs.length} entries`;
    }
  }

  /**
   * Scroll a container to the bottom.
   */
  function scrollToBottom(selector) {
    const el = $(selector);
    if (el) {
      el.scrollTop = el.scrollHeight;
    }
  }

  // ─── Public API ────────────────────────────────────────────────────────

  return {
    init,
    startStreaming,
    stopStreaming,
    renderBrowserLogs,
  };
})();
