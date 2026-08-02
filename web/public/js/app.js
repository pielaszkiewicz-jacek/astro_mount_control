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

  const { $, $$, formatTime, enhanceAllAngleInputs } = Utils;

  // ─── State ────────────────────────────────────────────────────────────
  let pollInterval = null;
  let isConnected = false;
  let lastState = null;
  let dbConnected = false;
  // Polling interval in milliseconds
  const POLL_INTERVAL_MS = 1000;
  // Cached mount type from config (0=EQUATORIAL, 1=ALT_AZ, 3=CASUAL)
  let cachedMountType = null;

  // ─── Initialization ───────────────────────────────────────────────────

  /**
   * Initialize the application.
   * Called when the DOM is ready.
   */
  function init() {
    Logger.init(); // Must be first — wraps console methods to capture all output
    console.log('[App] Logger.init() done');
    I18n.init();
    console.log('[App] I18n.init() done');
    initTabs();
    initRedThemeToggle();
    initMobileModeToggle();
    initFullscreenToggle();
    initLangToggle();
    // Enhance angle inputs with DMS/HMS format support
    Utils.enhanceAllAngleInputs();
    MountControlComponent.init();
    console.log('[App] MountControlComponent.init() done');
    DatabaseComponent.init();
    console.log('[App] DatabaseComponent.init() done');
    CalibrationComponent.init();
    console.log('[App] CalibrationComponent.init() done');
    TrackingComponent.init();
    console.log('[App] TrackingComponent.init() done');
    LoggingComponent.init();
    console.log('[App] LoggingComponent.init() done');
    DebugTestComponent.init();
    console.log('[App] DebugTestComponent.init() done');
    SettingsComponent.initAddressForm();
    applyExternalServicesVisibility();
    mountExtendedComponents();
    initTabScroll();
    startPolling();
  }

  /**
   * Initialize tab scroll buttons — show/hide based on scroll position
   * and scroll the tab bar left/right on click.
   */
  function initTabScroll() {
    const nav = document.getElementById('tab-nav');
    const scrollLeft = document.getElementById('tab-scroll-left');
    const scrollRight = document.getElementById('tab-scroll-right');
    if (!nav || !scrollLeft || !scrollRight) return;

    function updateScrollButtons() {
      const atStart = nav.scrollLeft <= 4;
      const atEnd = nav.scrollLeft + nav.clientWidth >= nav.scrollWidth - 4;
      scrollLeft.classList.toggle('hidden', atStart);
      scrollRight.classList.toggle('hidden', atEnd);
    }

    const scrollAmount = () => Math.max(200, nav.clientWidth * 0.6);

    scrollLeft.addEventListener('click', () => {
      nav.scrollBy({ left: -scrollAmount(), behavior: 'smooth' });
    });

    scrollRight.addEventListener('click', () => {
      nav.scrollBy({ left: scrollAmount(), behavior: 'smooth' });
    });

    nav.addEventListener('scroll', updateScrollButtons);
    window.addEventListener('resize', updateScrollButtons);

    // Initial check after layout settles
    setTimeout(updateScrollButtons, 100);
    setTimeout(updateScrollButtons, 500);
  }

  /**
   * External service tab → panel/button ID mapping.
   * Used to show/hide UI elements based on which services are enabled.
   */
  const EXT_SERVICE_TABS = {
    power:     { tab: 'tab-power',     panel: 'panel-power',     btn: '.tab-btn[data-tab="power"]' },
    sequencer: { tab: 'tab-sequencer', panel: 'panel-sequencer', btn: '.tab-btn[data-tab="sequencer"]' },
    weather:   { tab: 'tab-weather',   panel: 'panel-weather',   btn: '.tab-btn[data-tab="weather"]' },
    dome:      { tab: 'tab-dome',      panel: 'panel-dome',      btn: '.tab-btn[data-tab="dome"]' },
    derotator: { tab: 'tab-derotator', panel: 'panel-derotator', btn: '.tab-btn[data-tab="derotator"]' },
    focuser:   { tab: 'tab-focuser',   panel: 'panel-focuser',   btn: '.tab-btn[data-tab="focuser"]' },
  };

  /**
   * Fetch external services config and hide tabs for disabled services.
   */
  async function applyExternalServicesVisibility() {
    try {
      const resp = await fetch('/api/config/external-services');
      const config = await resp.json();
      Object.keys(EXT_SERVICE_TABS).forEach(key => {
        const ids = EXT_SERVICE_TABS[key];
        const enabled = config[key] === true;
        // Hide/show the tab button
        const btn = document.querySelector(ids.btn);
        if (btn) btn.style.display = enabled ? '' : 'none';
        // Hide/show the panel
        const panel = document.getElementById(ids.panel);
        if (panel) panel.style.display = enabled ? '' : 'none';
        console.log('[App] external service', key, enabled ? 'enabled' : 'disabled');
      });
    } catch (err) {
      console.warn('[App] Failed to load external services config:', err.message);
    }
  }

  /**
   * Mount extended service components into their respective panel containers.
   * Each component exposes a render() method that returns HTML to be injected.
   */
  function mountExtendedComponents() {
    const mounts = [
      { id: 'pec-component-mount',      render: PECComponent.render },
      { id: 'power-component-mount',    render: PowerComponent.render },
      { id: 'guider-component-mount',   render: GuiderComponent.render },
      { id: 'derotator-component-mount', render: DerotatorComponent.render },
      { id: 'sequencer-component-mount', render: SequencerComponent.render },
      { id: 'camera-component-mount',   render: CameraComponent.render },
      { id: 'focuser-component-mount',  render: FocuserComponent.render },
      { id: 'dome-component-mount',     render: DomeComponent.render },
      { id: 'weather-component-mount',  render: WeatherComponent.render },
      { id: 'pulley-component-mount',   render: PulleyComponent.render },
    ];

    mounts.forEach(m => {
      const el = document.getElementById(m.id);
      if (el) {
        el.innerHTML = m.render();
        console.log('[App] mounted component:', m.id);
      }
    });
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

        // Scroll active tab into view within the scrollable nav
        const tabNav = document.getElementById('tab-nav');
        if (tabNav) {
          const tabRect = tab.getBoundingClientRect();
          const navRect = tabNav.getBoundingClientRect();
          if (tabRect.left < navRect.left || tabRect.right > navRect.right) {
            tab.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' });
          }
          // Update scroll buttons after a brief delay
          setTimeout(() => {
            const sl = document.getElementById('tab-scroll-left');
            const sr = document.getElementById('tab-scroll-right');
            if (sl && sr) {
              const atStart = tabNav.scrollLeft <= 4;
              const atEnd = tabNav.scrollLeft + tabNav.clientWidth >= tabNav.scrollWidth - 4;
              sl.classList.toggle('hidden', atStart);
              sr.classList.toggle('hidden', atEnd);
            }
          }, 350);
        }

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
          DatabaseComponent.loadObjects();
        }

        // Redraw velocity chart when Status tab becomes visible.
        // When the tab is hidden (display:none), canvas dimensions are 0×0
        // and drawChart() skips rendering. On reactivation, the chart must
        // be restored from buffered data.
        if (tabName === 'status') {
          MountStatusComponent.redrawVelocityChart();
        }

        // Start/stop calibration polling when tab is shown/hidden
        if (tabName === 'calibration') {
          CalibrationComponent.startPolling();
          // Re-run angle enhancement for dynamically-shown calibration inputs
          Utils.enhanceAllAngleInputs(document.getElementById('panel-calibration'));
        } else {
          CalibrationComponent.stopPolling();
        }

        // Start/stop tracking polling when tab is shown/hidden
        if (tabName === 'tracking') {
          TrackingComponent.startPolling();
        } else {
          TrackingComponent.stopPolling();
        }

        // Start/stop log streaming when tab is shown/hidden
        if (tabName === 'logging') {
          LoggingComponent.startStreaming();
          LoggingComponent.renderBrowserLogs();
        } else {
          LoggingComponent.stopStreaming();
        }

        // ── Extended service tab lazy-loading ──
        if (tabName === 'power') {
          PowerComponent.refresh();
        }
        if (tabName === 'guider') {
          GuiderComponent.refreshStatus();
        }
        if (tabName === 'derotator') {
          DerotatorComponent.refreshStatus();
        }
        if (tabName === 'sequencer') {
          SequencerComponent.refreshStatus();
        }
        if (tabName === 'camera') {
          CameraComponent.refreshInfo();
        }
        if (tabName === 'focuser') {
          FocuserComponent.refreshStatus();
        }
        if (tabName === 'pec') {
          PECComponent.refreshStatus();
        }
        if (tabName === 'dome') {
          DomeComponent.refreshStatus();
        }
        if (tabName === 'weather') {
          WeatherComponent.refresh();
        }
        if (tabName === 'pulley') {
          PulleyComponent.refreshStatus();
        }

      });
    });
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

  /**
   * Initialize the fullscreen toggle button.
   * Uses the Fullscreen API to toggle fullscreen mode on the document.
   * The SVG icon swaps between expand and collapse icons based on state.
   * Listens for the 'fullscreenchange' event to stay in sync with
   * browser-initiated fullscreen changes (e.g., pressing F11/Esc).
   */
  function initFullscreenToggle() {
    const toggle = $('#fullscreen-toggle');
    if (!toggle) return;

    const expandIcon = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="15 3 21 3 21 9"/><polyline points="9 21 3 21 3 15"/><line x1="21" y1="3" x2="14" y2="10"/><line x1="3" y1="21" x2="10" y2="14"/></svg>`;
    const collapseIcon = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="4 8 4 3 9 3"/><polyline points="20 16 20 21 15 21"/><line x1="4" y1="3" x2="11" y2="10"/><line x1="20" y1="21" x2="13" y2="14"/></svg>`;

    function updateIcon() {
      const isFullscreen = !!document.fullscreenElement;
      toggle.innerHTML = isFullscreen ? collapseIcon : expandIcon;
      toggle.classList.toggle('active', isFullscreen);
      toggle.title = isFullscreen ? 'Exit fullscreen mode' : 'Toggle fullscreen mode';
    }

    // Sync on load (in case F11 was pressed before the page loaded)
    updateIcon();

    // Listen for browser-initiated fullscreen changes
    document.addEventListener('fullscreenchange', updateIcon);

    toggle.addEventListener('click', () => {
      if (!document.fullscreenElement) {
        document.documentElement.requestFullscreen().catch((err) => {
          console.warn('[App] Fullscreen request failed:', err.message);
        });
      } else {
        document.exitFullscreen();
      }
    });
  }

  /**
   * Initialize the language toggle button.
   * Switches between English (en) and Polish (pl).
   */
  function initLangToggle() {
    const toggle = $('#lang-toggle');
    if (!toggle) return;

    toggle.addEventListener('click', () => {
      I18n.toggleLang();
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
      MountControlComponent.setCalibrationState(state, await getMountType(state));
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
  }

  /**
   * Infer mount type from the controller configuration.
   * Fetches the config on first call and caches the result.
   * @param {object} state - Controller state from API
   * @returns {'equatorial'|'alt_az'|'casual'|'unknown'}
   */
  async function getMountType(state) {
    if (cachedMountType !== null) {
      return cachedMountType;
    }
    try {
      const config = await Api.getConfig();
      let rawType = config.mount_type;
      // gRPC may serialize as string ("CASUAL") or number (3)
      if (typeof rawType === 'string') {
        rawType = rawType.toUpperCase();
      }
      if (rawType === 'CASUAL' || rawType === 3) {
        cachedMountType = 'casual';
      } else if (rawType === 'ALT_AZ' || rawType === 1) {
        cachedMountType = 'alt_az';
      } else {
        cachedMountType = 'equatorial';
      }
    } catch (e) {
      console.warn('[App] Failed to fetch mount type from config:', e.message);
      cachedMountType = 'equatorial';
    }
    return cachedMountType;
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
