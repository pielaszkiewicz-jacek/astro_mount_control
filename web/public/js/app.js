/**
 * Astronomical Mount Controller - Web Interface Main Application
 *
 * Core application logic:
 * - Tab navigation system
 * - Polling loop for real-time status updates
 * - Connection health monitoring
 * - Toast notification system
 *
 * Framework for future cards:
 * To add a new card/tab:
 * 1. Create a new component file in js/components/
 * 2. Add the tab button in index.html (tab-nav)
 * 3. Add the tab panel with card(s) in index.html
 * 4. Initialize the component in App.init()
 */

const App = (() => {
  'use strict';

  const { $, $$, formatTime } = Utils;

  // ─── State ────────────────────────────────────────────────────────────
  let pollInterval = null;
  let isConnected = false;
  let lastState = null;
  let dbConnected = false;
  // Polling interval in milliseconds
  const POLL_INTERVAL_MS = 1000;

  // ─── Initialization ───────────────────────────────────────────────────

  /**
   * Initialize the application.
   * Called when the DOM is ready.
   */
  function init() {
    Logger.init(); // Must be first — wraps console methods to capture all output
    console.log('[App] Logger.init() done');
    initTabs();
    initRedThemeToggle();
    initMobileModeToggle();
    initLogPanel();
    MountControlComponent.init();
    console.log('[App] MountControlComponent.init() done');
    DatabaseComponent.init();
    console.log('[App] DatabaseComponent.init() done');
    CalibrationComponent.init();
    console.log('[App] CalibrationComponent.init() done');
    TrackingComponent.init();
    console.log('[App] TrackingComponent.init() done');
    SettingsComponent.initAddressForm();
    startPolling();
  }

  // ─── Tab System ───────────────────────────────────────────────────────

  /**
   * Initialize the tab navigation system.
   * Clicking a tab shows its corresponding panel and hides others.
   */
  function initTabs() {
    console.log('[App] initTabs() called');
    const tabs = $$('.tab-btn:not([disabled])');
    console.log('[App] found', tabs.length, 'tab buttons');

    tabs.forEach(tab => {
      tab.addEventListener('click', () => {
        const tabName = tab.dataset.tab;
        console.log('[App] tab clicked:', tabName);
        if (!tabName) return;

        // Deactivate all tabs and panels
        $$('.tab-btn').forEach(t => {
          t.classList.remove('active');
          t.setAttribute('aria-selected', 'false');
        });
        $$('.tab-panel').forEach(p => p.classList.remove('active'));

        // Activate selected tab and panel
        tab.classList.add('active');
        tab.setAttribute('aria-selected', 'true');

        const panel = $(`#panel-${tabName}`);
        if (panel) {
          console.log('[App] activating panel:', '#panel-' + tabName);
          panel.classList.add('active');
          console.log('[App] panel classes after active:', panel.className);
        } else {
          console.warn('[App] panel NOT FOUND:', '#panel-' + tabName);
        }

        // Lazy-load settings data when tab is first shown
        if (tabName === 'settings') {
          SettingsComponent.loadConfig();
          SettingsComponent.loadAddresses();
        }

        // Lazy-load database data when tab is first shown
        if (tabName === 'database') {
          DatabaseComponent.loadStats();
        }

        // Start/stop calibration polling when tab is shown/hidden
        if (tabName === 'calibration') {
          CalibrationComponent.startPolling();
        } else {
          CalibrationComponent.stopPolling();
        }

        // Start/stop tracking polling when tab is shown/hidden
        if (tabName === 'tracking') {
          TrackingComponent.startPolling();
        } else {
          TrackingComponent.stopPolling();
        }

      });
    });
  }

  // ─── Log Panel ────────────────────────────────────────────────────────

  /**
   * Initialize the collapsible log panel in the footer.
   */
  function initLogPanel() {
    const toggleBtn = $('#btn-toggle-log');
    const closeBtn = $('#btn-close-log');
    const clearBtn = $('#btn-clear-log');
    const panel = $('#log-panel');
    if (!toggleBtn || !panel) return;

    // Toggle log panel visibility
    toggleBtn.addEventListener('click', () => {
      const isHidden = panel.style.display === 'none' || !panel.style.display;
      panel.style.display = isHidden ? 'flex' : 'none';
      toggleBtn.textContent = isHidden ? '📋 Hide Logs' : '📋 Logs';
      if (isHidden) {
        renderLogContent();
      }
    });

    // Close button
    if (closeBtn) {
      closeBtn.addEventListener('click', () => {
        panel.style.display = 'none';
        toggleBtn.textContent = '📋 Logs';
      });
    }

    // Clear button
    if (clearBtn) {
      clearBtn.addEventListener('click', () => {
        Logger.clearLogs();
        renderLogContent();
      });
    }
  }

  /**
   * Render the current log entries into the log panel content area.
   */
  function renderLogContent() {
    const content = $('#log-content');
    if (!content) return;
    content.innerHTML = Logger.getLogHtml();
    // Auto-scroll to bottom
    content.scrollTop = content.scrollHeight;
  }

  // ─── Red/Night-Vision Theme Toggle ─────────────────────────────────────

  /**
   * Initialize the red/night-vision theme toggle.
   * Reads the saved preference from localStorage and applies it on load.
   * Clicking the toggle adds/removes the `red-theme` class on <body>.
   */
  function initRedThemeToggle() {
    const toggle = $('#theme-toggle');
    if (!toggle) return;

    // Apply saved preference on load
    const saved = localStorage.getItem('red-theme');
    if (saved === 'true') {
      document.body.classList.add('red-theme');
      toggle.classList.add('active');
    }

    toggle.addEventListener('click', () => {
      const isActive = document.body.classList.toggle('red-theme');
      toggle.classList.toggle('active', isActive);
      localStorage.setItem('red-theme', isActive ? 'true' : 'false');
    });
  }

  /**
   * Initialize the mobile layout mode toggle.
   * Adds/removes the `mobile-mode` class on <body> to force single-column
   * layout regardless of actual viewport width.
   * Saves preference to localStorage.
   */
  function initMobileModeToggle() {
    const toggle = $('#mobile-toggle');
    if (!toggle) return;

    // Apply saved preference on load
    const saved = localStorage.getItem('mobile-mode');
    if (saved === 'true') {
      document.body.classList.add('mobile-mode');
      toggle.classList.add('active');
    }

    toggle.addEventListener('click', () => {
      const isActive = document.body.classList.toggle('mobile-mode');
      toggle.classList.toggle('active', isActive);
      localStorage.setItem('mobile-mode', isActive ? 'true' : 'false');
    });
  }

  // ─── Polling Loop ─────────────────────────────────────────────────────

  /**
   * Start the periodic status polling loop.
   */
  function startPolling() {
    poll();
    pollInterval = setInterval(poll, POLL_INTERVAL_MS);
  }

  /**
   * Stop the polling loop.
   */
  function stopPolling() {
    if (pollInterval) {
      clearInterval(pollInterval);
      pollInterval = null;
    }
  }

  /**
   * Fetch the latest status from the API and update all components.
   */
  async function poll() {
    try {
      const state = await Api.getStatus();
      isConnected = true;
      lastState = state;
      updateConnectionBadge(true);
      MountStatusComponent.render(state);
      MountControlComponent.setCalibrationState(state, getMountType(state));
      updateFooterTime();
    } catch (err) {
      isConnected = false;
      updateConnectionBadge(false);
      MountStatusComponent.render(null);
    }

    // Check database connection independently (does not affect mount polling)
    try {
      const dbOk = await Api.checkDbHealth();
      dbConnected = dbOk;
      updateDbConnectionBadge(dbOk);
    } catch {
      dbConnected = false;
      updateDbConnectionBadge(false);
    }

    // Refresh log panel content if visible
    const logPanel = $('#log-panel');
    if (logPanel && logPanel.style.display === 'flex') {
      renderLogContent();
    }
  }

  /**
   * Infer mount type from the controller state.
   * @param {object} state - Controller state from API
   * @returns {'equatorial'|'alt_az'|'unknown'}
   */
  function getMountType(state) {
    // pier_side is only meaningful for equatorial mounts
    if (state && state.pier_side !== undefined && state.pier_side !== null) {
      return 'equatorial';
    }
    return 'equatorial'; // Safe default
  }

  // ─── Connection Badge ─────────────────────────────────────────────────

  /**
   * Update the connection indicator in the header.
   * @param {boolean} connected
   */
  function updateConnectionBadge(connected) {
    const badge = $('#connection-indicator');
    if (!badge) return;

    if (connected) {
      badge.className = 'connection-badge connected';
      badge.querySelector('.badge-label').textContent = 'Connected';
    } else {
      badge.className = 'connection-badge disconnected';
      badge.querySelector('.badge-label').textContent = 'Disconnected';
    }
  }

  /**
   * Update the database connection indicator in the header.
   * @param {boolean} connected
   */
  function updateDbConnectionBadge(connected) {
    const badge = $('#db-connection-indicator');
    if (!badge) return;

    if (connected) {
      badge.className = 'connection-badge connection-badge-db connected';
      badge.querySelector('.badge-label').textContent = 'DB On';
    } else {
      badge.className = 'connection-badge connection-badge-db disconnected';
      badge.querySelector('.badge-label').textContent = 'DB Off';
    }
  }

  // ─── Footer Timestamp ─────────────────────────────────────────────────

  /**
   * Update the footer with the last update time.
   */
  function updateFooterTime() {
    const el = $('#last-update');
    if (el) {
      el.textContent = `Last update: ${formatTime(new Date())}`;
    }
  }

  // ─── Toast Notification System ────────────────────────────────────────

  /**
   * Show a toast notification.
   *
   * @param {string} message - Notification text
   * @param {'success'|'error'|'info'} type - Toast type
   * @param {number} [duration=3000] - Display duration in ms
   */
  function showToast(message, type = 'info', duration = 3000) {
    const container = $('#toast-container');
    if (!container) return;

    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;

    container.appendChild(toast);

    // Auto-remove after duration
    setTimeout(() => {
      toast.classList.add('fade-out');
      setTimeout(() => {
        if (toast.parentNode) {
          toast.parentNode.removeChild(toast);
        }
      }, 250);
    }, duration);
  }

  // ─── Public API ───────────────────────────────────────────────────────

  /**
   * Get the last known controller state.
   * Used by MountControlComponent for calibrated nudges.
   * @returns {object|null}
   */
  function getLastState() {
    return lastState;
  }

  return {
    init,
    showToast,
    getLastState,
  };
})();

// ─── Boot ────────────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  App.init();
});
