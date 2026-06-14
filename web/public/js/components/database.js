/**
 * Astronomical Mount Controller - Database Component
 *
 * Manages the object database tab:
 * - Browse/search astronomical objects
 * - View object details (coordinates, magnitudes, type)
 * - Create/update/delete objects
 * - Manage favorites
 * - View database statistics
 * - Slew to selected object
 */
const DatabaseComponent = (() => {
  'use strict';

  const { $, $$, formatRA, formatDec, formatNumber } = Utils;

  // ─── State ────────────────────────────────────────────────────────────
  let currentPage = 1;
  let pageSize = 20;
  let totalPages = 1;
  let totalCount = 0;
  let currentObjects = [];
  let currentDetail = null;
  let currentSearchQuery = '';
  let currentTypeFilter = '';
  let currentMagMax = '';
  let currentMagMin = '';
  let currentSortBy = 'name';
  let currentSortDesc = false;
  let currentFavoritesOnly = false;
  let currentVisibleOnly = false;
  let currentCatalogs = '';
  let currentConstellation = '';

  // ─── Help Content ─────────────────────────────────────────────────────

  function buildDatabaseHelpContent() {
    const container = $('#database-help-content');
    if (!container) return;

    const t = I18n.t.bind(I18n);

    const steps = [
      { num: '1', titleKey: 'database.help_step1_title', open: true,
        bodyHtml: '<ol><li>' + t('database.help_step1_li1') + '</li><li>' + t('database.help_step1_li2') + '</li><li>' + t('database.help_step1_li3') + '</li></ol>' },
      { num: '2', titleKey: 'database.help_step2_title', open: false,
        bodyHtml: '<ol><li>' + t('database.help_step2_li1') + '</li><li>' + t('database.help_step2_li2') + '</li><li>' + t('database.help_step2_li3') + '</li></ol>' },
      { num: '3', titleKey: 'database.help_step3_title', open: false,
        bodyHtml: '<ol><li>' + t('database.help_step3_li1') + '</li><li>' + t('database.help_step3_li2') + '</li><li>' + t('database.help_step3_li3') + '</li></ol>' },
      { num: '4', titleKey: 'database.help_step4_title', open: false,
        bodyHtml: '<ul><li>' + t('database.help_step4_li1') + '</li><li>' + t('database.help_step4_li2') + '</li><li>' + t('database.help_step4_li3') + '</li></ul>' }
    ];

    let html = '<p><strong>' + t('database.help_purpose_label') + '</strong> ' + t('database.help_purpose_text') + '</p>';

    steps.forEach(function(step) {
      html += '<details class="calibration-help-step"' + (step.open ? ' open' : '') + '>'
        + '<summary class="calibration-help-step-summary">'
        + '<span class="calibration-help-step-number">' + step.num + '</span>'
        + t(step.titleKey) + '</summary>'
        + '<div class="calibration-help-step-body">' + step.bodyHtml + '</div>'
        + '</details>';
    });

    container.innerHTML = html;
  }

  // ─── Initialization ───────────────────────────────────────────────────

  function init() {
    buildDatabaseHelpContent();
    document.addEventListener('i18n:applied', buildDatabaseHelpContent);
    bindHelpToggle('card-database-help');
    bindEvents();
    loadStats();
  }

  function bindHelpToggle(cardId) {
    const card = $('#' + cardId);
    if (!card) return;
    const toggleBtn = card.querySelector('.card-toggle-btn');
    const header = card.querySelector('.card-header');
    const doToggle = function() {
      const collapsed = card.classList.toggle('card-collapsed');
      if (toggleBtn) toggleBtn.textContent = collapsed ? '+' : '\u2212';
    };
    if (toggleBtn) { toggleBtn.addEventListener('click', function(e) { e.stopPropagation(); doToggle(); }); }
    if (header) { header.addEventListener('click', function(e) { if (e.target.closest('button, input, select, a, label')) return; doToggle(); }); }
  }

  // ─── Event Binding ────────────────────────────────────────────────────

  function bindEvents() {
    // Search
    $('#btn-db-search').addEventListener('click', handleSearch);
    $('#db-search-input').addEventListener('keydown', (e) => {
      if (e.key === 'Enter') handleSearch();
    });

    // Filter changes trigger search
    $('#db-type-filter').addEventListener('change', handleSearch);
    $('#db-mag-max').addEventListener('change', handleSearch);
    $('#db-mag-min').addEventListener('change', handleSearch);
    $('#db-sort-by').addEventListener('change', handleSearch);
    $('#db-sort-desc').addEventListener('change', handleSearch);
    $('#db-fav-only').addEventListener('change', handleSearch);
    $('#db-visible-only').addEventListener('change', handleSearch);
    $('#db-catalogs').addEventListener('change', handleSearch);
    $('#db-constellation').addEventListener('change', handleSearch);

    // Pagination
    $('#btn-db-prev').addEventListener('click', () => goToPage(currentPage - 1));
    $('#btn-db-next').addEventListener('click', () => goToPage(currentPage + 1));

    // Page number buttons (event delegation)
    $('#db-page-numbers').addEventListener('click', handlePageNumberClick);

    // Page size selector
    $('#db-page-size').addEventListener('change', handlePageSizeChange);

    // Create form
    $('#db-create-form').addEventListener('submit', handleCreate);

    // Detail actions
    $('#btn-db-slew').addEventListener('click', handleSlewToDetail);
    $('#btn-db-fav').addEventListener('click', handleToggleFavorite);
    $('#btn-db-edit').addEventListener('click', handleEditObject);
    $('#btn-db-delete').addEventListener('click', handleDeleteObject);
    $('#btn-db-close-detail').addEventListener('click', closeDetail);

    // Edit form
    $('#db-edit-form').addEventListener('submit', handleUpdate);
    $('#btn-db-edit-cancel').addEventListener('click', handleCancelEdit);
    $('#btn-db-edit-cancel-form').addEventListener('click', handleCancelEdit);

    // Create form
    const newType = $('#db-new-type');
    if (newType) {
      newType.addEventListener('change', () => {});
    }

    // Collapse/expand "Add New Object" panel
    $('#btn-db-create-toggle').addEventListener('click', toggleCreatePanel);

    // Collapse/expand "Database Stats" panel
    $('#btn-db-stats-toggle').addEventListener('click', toggleStatsPanel);

    // ── Import Events ──
    const importToggle = $('#btn-db-import-toggle');
    if (importToggle) {
      importToggle.addEventListener('click', toggleImportPanel);
    }
    const btnImportFile = $('#btn-db-import-file');
    if (btnImportFile) {
      btnImportFile.addEventListener('click', handleFileImport);
    }
    const btnImportUrl = $('#btn-db-import-url');
    if (btnImportUrl) {
      btnImportUrl.addEventListener('click', handleUrlImport);
    }
  }

  // ─── Load Database Stats ──────────────────────────────────────────────

  async function loadStats() {
    const content = $('#db-stats-content');
    const badge = $('#db-stats-badge');
    if (!content) return;

    try {
      const stats = await Api.getDbStats();
      content.innerHTML = buildStatsHtml(stats);
      if (badge) {
        badge.textContent = `${stats.total_objects} objects`;
        badge.className = 'status-badge idle';
      }
    } catch (err) {
      content.innerHTML = `<div class="status-placeholder" style="color:var(--color-danger);">${I18n.t('db.unavailable', { message: err.message })}</div>`;
      if (badge) {
        badge.textContent = I18n.t('status.offline');
        badge.className = 'status-badge error';
      }
    }
  }

  // Map numeric ObjectType enum values to readable enum name strings.
  // The C++ backend stores type keys as std::to_string(enum_value), so we get
  // keys like "1" (STAR), "11" (GALAXY_SPIRAL) instead of the enum names.
  const OBJECT_TYPE_MAP = {
    '0': 'UNKNOWN_TYPE',
    '1': 'STAR',
    '2': 'DOUBLE_STAR',
    '3': 'VARIABLE_STAR',
    '4': 'STAR_CLUSTER_OPEN',
    '5': 'STAR_CLUSTER_GLOBULAR',
    '6': 'PLANETARY_NEBULA',
    '7': 'DIFFUSE_NEBULA',
    '8': 'DARK_NEBULA',
    '9': 'EMISSION_NEBULA',
    '10': 'REFLECTION_NEBULA',
    '11': 'GALAXY_SPIRAL',
    '12': 'GALAXY_ELLIPTICAL',
    '13': 'GALAXY_IRREGULAR',
    '14': 'GALAXY_LENTICULAR',
    '15': 'QUASAR',
    '16': 'SUPERNOVA_REMNANT',
    '17': 'PLANET',
    '18': 'DWARF_PLANET',
    '19': 'MOON',
    '20': 'ASTEROID',
    '21': 'COMET',
    '22': 'SATELLITE',
    '23': 'EXOPLANET',
    '24': 'ARTIFICIAL_SATELLITE',
    '25': 'SPACE_DEBRIS',
  };

  function buildStatsHtml(stats) {
    const byType = stats.objects_by_type || {};
    const typeKeys = Object.keys(byType).slice(0, 8);
    const byCatalog = stats.objects_by_catalog || {};
    const catalogKeys = Object.keys(byCatalog).slice(0, 5);

    let html = '';
    html += `<div class="stat-row"><span class="stat-label">Total Objects</span><span class="stat-value highlight">${stats.total_objects || 0}</span></div>`;
    html += `<div class="stat-row"><span class="stat-label">Favorites</span><span class="stat-value">${stats.favorite_count || 0}</span></div>`;
    html += `<div class="stat-row"><span class="stat-label">User Added</span><span class="stat-value">${stats.user_added_count || 0}</span></div>`;

    if (stats.avg_magnitude !== undefined && stats.avg_magnitude !== null) {
      html += `<div class="stat-row"><span class="stat-label">Avg Magnitude</span><span class="stat-value">${formatNumber(stats.avg_magnitude, 2)}</span></div>`;
    }

    if (typeKeys.length > 0) {
      html += `<div class="stat-row db-stat-section"><span class="stat-label">By Type</span></div>`;
      typeKeys.forEach(key => {
        const typeName = OBJECT_TYPE_MAP[key] || key;
        html += `<div class="stat-row db-stat-sub"><span class="stat-label">${formatTypeName(typeName)}</span><span class="stat-value">${byType[key]}</span></div>`;
      });
    }

    if (catalogKeys.length > 0) {
      html += `<div class="stat-row db-stat-section"><span class="stat-label">By Catalog</span></div>`;
      catalogKeys.forEach(key => {
        html += `<div class="stat-row db-stat-sub"><span class="stat-label">${key}</span><span class="stat-value">${byCatalog[key]}</span></div>`;
      });
    }

    if (stats.last_update) {
      const date = new Date(stats.last_update.seconds * 1000 || stats.last_update);
      html += `<div class="stat-row"><span class="stat-label">Last Update</span><span class="stat-value">${date.toLocaleDateString()}</span></div>`;
    }

    return html;
  }

  // ─── Search / Browse ──────────────────────────────────────────────────

  async function handleSearch() {
    currentSearchQuery = $('#db-search-input').value.trim();
    currentTypeFilter = $('#db-type-filter').value;
    currentMagMax = $('#db-mag-max').value;
    currentMagMin = $('#db-mag-min').value;
    currentSortBy = $('#db-sort-by').value;
    currentSortDesc = $('#db-sort-desc').checked;
    currentFavoritesOnly = $('#db-fav-only').checked;
    currentVisibleOnly = $('#db-visible-only').checked;
    currentCatalogs = $('#db-catalogs').value.trim();
    currentConstellation = $('#db-constellation').value;
    currentPage = 1;
    await loadObjects();
  }

  async function loadObjects() {
    const listContent = $('#db-list-content');
    if (!listContent) return;

    listContent.innerHTML = '<div class="status-placeholder">Loading objects...</div>';

    // Common filter params
    const filterParams = {
      minMagnitude: currentMagMin ? parseFloat(currentMagMin) : undefined,
      maxMagnitude: currentMagMax ? parseFloat(currentMagMax) : undefined,
    };

    try {
      let result;

      // Use search endpoint when query is present OR when filter-only features
      // (favoritesOnly, visibleOnly) are active — listObjects doesn't support them.
      // Use search endpoint when query, filter flags, or catalogs filter is active
      // (listObjects endpoint does not support catalog filtering)
      if (currentSearchQuery || currentFavoritesOnly || currentVisibleOnly || currentCatalogs || currentConstellation) {
        const catalogsArr = currentCatalogs
          ? currentCatalogs.split(',').map(s => s.trim()).filter(Boolean)
          : undefined;

        result = await Api.searchObjects({
          query: currentSearchQuery || undefined,
          objectType: currentTypeFilter || undefined,
          ...filterParams,
          favoritesOnly: currentFavoritesOnly || undefined,
          visibleOnly: currentVisibleOnly || undefined,
          catalogs: catalogsArr,
          constellation: currentConstellation || undefined,
        });
      } else {
        // Use list endpoint for browsing
        result = await Api.listObjects({
          page: currentPage,
          pageSize,
          filterType: currentTypeFilter || undefined,
          ...filterParams,
          sortBy: currentSortBy || 'name',
          sortDescending: currentSortDesc || undefined,
        });
      }

      currentObjects = result.objects || [];
      totalCount = result.total_count || currentObjects.length;
      totalPages = result.total_pages || Math.ceil(totalCount / pageSize) || 1;

      renderObjectList();
      updatePagination();
      updateListCount();

      // Auto-load stats on first load
      if (totalCount > 0) loadStats();
    } catch (err) {
      listContent.innerHTML = `<div class="status-placeholder" style="color:var(--color-danger);">Error: ${err.message}</div>`;
    }
  }

  function renderObjectList() {
    const container = $('#db-list-content');
    if (!container) return;

    if (currentObjects.length === 0) {
      container.innerHTML = '<div class="status-placeholder">No objects found. Try a different search.</div>';
      return;
    }

    let html = '<div class="db-list">';
    currentObjects.forEach(obj => {
      const isFav = obj.is_favorite;
      const mag = obj.v_magnitude !== undefined && obj.v_magnitude !== null && obj.v_magnitude !== 0
        ? formatNumber(obj.v_magnitude, 1) : '—';
      const type = obj.object_type || 'UNKNOWN';
      const ra = obj.ra_hours !== undefined && obj.ra_hours !== null
        ? formatRA(obj.ra_hours) : '—';
      const dec = obj.dec_degrees !== undefined && obj.dec_degrees !== null
        ? formatDec(obj.dec_degrees) : '—';

      html += `<div class="db-list-item" data-id="${escapeHtml(obj.id || obj.name)}" data-rawname="${escapeHtml(obj.name || '')}">`;
      html += `<div class="db-item-main" onclick="DatabaseComponent.selectObject('${escapeHtml(obj.id || obj.name)}')">`;
      html += `<div class="db-item-name">${escapeHtml(obj.name || '(unnamed)')}`;
      if (obj.catalog_id) html += ` <span class="db-item-catalog">${escapeHtml(obj.catalog_id)}</span>`;
      html += `</div>`;
      const constel = obj.custom_fields && obj.custom_fields.constellation
        ? escapeHtml(obj.custom_fields.constellation) : null;

      html += `<div class="db-item-meta">`;
      html += `<span class="db-item-type">${formatTypeName(type)}</span>`;
      html += `<span class="db-item-mag">${mag}</span>`;
      html += `<span class="db-item-coord">${ra} ${dec}</span>`;
      if (constel) html += `<span class="db-item-constellation">${constel}</span>`;
      html += `</div></div>`;
      html += `<div class="db-item-actions">`;
      html += `<button class="btn btn-sm ${isFav ? 'btn-fav' : 'btn-secondary'}" onclick="DatabaseComponent.toggleFavorite('${escapeHtml(obj.id || obj.name)}')" title="${isFav ? 'Remove from favorites' : 'Add to favorites'}">${isFav ? '★' : '☆'}</button>`;
      html += `</div></div>`;
    });
    html += '</div>';
    container.innerHTML = html;
  }

  function updatePagination() {
    const pagination = $('#db-pagination');
    const prevBtn = $('#btn-db-prev');
    const nextBtn = $('#btn-db-next');
    const pageInfo = $('#db-page-info');
    const pageNumbers = $('#db-page-numbers');
    const paginationNav = $('#db-pagination-nav');
    const paginationInfo = $('#db-pagination-info');

    if (!pagination || !pageInfo || !pageNumbers) return;

    // Always show pagination bar (page size selector is always visible)
    pagination.style.display = 'flex';

    const hasMultiplePages = totalPages > 1;

    // Hide navigation buttons and page info when only one page
    if (paginationNav) paginationNav.style.display = hasMultiplePages ? 'flex' : 'none';
    if (paginationInfo) paginationInfo.style.display = hasMultiplePages ? 'flex' : 'none';

    if (!hasMultiplePages) return;

    pageInfo.textContent = I18n.t('db.page_of', { page: currentPage, total: totalPages });
    prevBtn.disabled = currentPage <= 1;
    nextBtn.disabled = currentPage >= totalPages;

    // Build page number buttons
    pageNumbers.innerHTML = buildPageNumberHtml();
  }

  /**
   * Build HTML for page number buttons with ellipsis.
   * Shows a sliding window of up to 5 pages around the current page,
   * always including first and last pages.
   */
  function buildPageNumberHtml() {
    let html = '';
    const total = totalPages;
    const current = currentPage;
    const maxVisible = 5; // max page buttons to show (excluding ellipsis and edges)

    // Determine the range of visible pages
    let start = Math.max(2, current - Math.floor(maxVisible / 2));
    let end = Math.min(total - 1, start + maxVisible - 1);
    if (end - start + 1 < maxVisible) {
      start = Math.max(2, end - maxVisible + 1);
    }

    // First page
    html += `<button class="db-page-btn${current === 1 ? ' active' : ''}" data-page="1">1</button>`;

    // Ellipsis before window if needed
    if (start > 2) {
      html += `<span class="db-page-btn ellipsis">⋯</span>`;
    }

    // Visible page range
    for (let i = start; i <= end; i++) {
      html += `<button class="db-page-btn${i === current ? ' active' : ''}" data-page="${i}">${i}</button>`;
    }

    // Ellipsis after window if needed
    if (end < total - 1) {
      html += `<span class="db-page-btn ellipsis">⋯</span>`;
    }

    // Last page (if more than 1 page)
    if (total > 1) {
      html += `<button class="db-page-btn${current === total ? ' active' : ''}" data-page="${total}">${total}</button>`;
    }

    return html;
  }

  /**
   * Handle click on page number buttons via event delegation.
   */
  function handlePageNumberClick(e) {
    const btn = e.target.closest('.db-page-btn');
    if (!btn || btn.classList.contains('active') || btn.classList.contains('ellipsis')) return;
    const page = parseInt(btn.dataset.page, 10);
    if (!isNaN(page)) {
      goToPage(page);
    }
  }

  function updateListCount() {
    const badge = $('#db-list-count');
    if (badge) {
      badge.textContent = `${totalCount}`;
      badge.className = 'status-badge idle';
    }
  }

  function goToPage(page) {
    if (page < 1 || page > totalPages) return;
    currentPage = page;
    loadObjects();
  }

  /**
   * Handle page size selector change.
   * Resets to page 1 and reloads with the new page size.
   */
  function handlePageSizeChange() {
    const select = $('#db-page-size');
    if (!select) return;
    const newSize = parseInt(select.value, 10);
    if (isNaN(newSize) || newSize < 1) return;

    // Only reload if the size actually changed
    if (newSize !== pageSize) {
      pageSize = newSize;
      currentPage = 1;
      loadObjects();
    }
  }

  // ─── Select Object (Show Detail) ──────────────────────────────────────

  async function selectObject(id) {
    const detailCard = $('#card-db-detail');
    const content = $('#db-detail-content');
    const title = $('#db-detail-title');
    if (!detailCard || !content) return;

    detailCard.style.display = 'block';
    content.innerHTML = '<div class="status-placeholder">Loading details...</div>';

    try {
      let object;
      // Try to get by ID first, then fall back to searching current list
      try {
        object = await Api.getObject(id);
      } catch (e) {
        // Fallback: find in current list
        object = currentObjects.find(o => o.id === id || o.name === id);
        if (!object) throw new Error('Object not found');
      }

      currentDetail = object;
      if (title) title.textContent = object.name || 'Object Details';
      renderDetail(object);
      updateFavoriteButton(object);
    } catch (err) {
      content.innerHTML = `<div class="status-placeholder" style="color:var(--color-danger);">${err.message}</div>`;
    }
  }

  function renderDetail(obj) {
    const content = $('#db-detail-content');
    if (!content) return;

    let html = '<div class="db-detail-grid">';

    // Basic Info
    html += '<div class="db-detail-section"><h3>Basic Info</h3>';
    html += detailRow('Name', obj.name || '—');
    html += detailRow('Type', formatTypeName(obj.object_type));
    html += detailRow('Catalog', obj.catalog_name || '—');
    html += detailRow('Catalog ID', obj.catalog_id || '—');
    html += detailRow('Alternate Names', obj.alternate_names || '—');
    // Constellation (stored in custom_fields)
    const constel = obj.custom_fields && obj.custom_fields.constellation;
    if (constel) {
      html += detailRow('Constellation', constel);
    }
    html += '</div>';

    // Coordinates
    html += '<div class="db-detail-section"><h3>Coordinates (J2000)</h3>';
    html += detailRow('RA', obj.ra_hours !== undefined && obj.ra_hours !== null ? `${formatRA(obj.ra_hours)} (${formatNumber(obj.ra_hours, 4)}h)` : '—');
    html += detailRow('Dec', obj.dec_degrees !== undefined && obj.dec_degrees !== null ? `${formatDec(obj.dec_degrees)} (${formatNumber(obj.dec_degrees, 2)}°)` : '—');
    if (obj.pm_ra) html += detailRow('PM RA', `${formatNumber(obj.pm_ra, 2)} mas/yr`);
    if (obj.pm_dec) html += detailRow('PM Dec', `${formatNumber(obj.pm_dec, 2)} mas/yr`);
    if (obj.parallax_mas) html += detailRow('Parallax', `${formatNumber(obj.parallax_mas, 2)} mas`);
    if (obj.distance_ly) html += detailRow('Distance', `${formatNumber(obj.distance_ly, 1)} ly`);
    html += '</div>';

    // Magnitudes
    html += '<div class="db-detail-section"><h3>Magnitudes</h3>';
    html += detailRow('V', obj.v_magnitude ? formatNumber(obj.v_magnitude, 2) : '—');
    html += detailRow('B', obj.b_magnitude ? formatNumber(obj.b_magnitude, 2) : '—');
    html += detailRow('J', obj.j_magnitude ? formatNumber(obj.j_magnitude, 2) : '—');
    html += detailRow('H', obj.h_magnitude ? formatNumber(obj.h_magnitude, 2) : '—');
    html += detailRow('K', obj.k_magnitude ? formatNumber(obj.k_magnitude, 2) : '—');
    html += '</div>';

    // Spectral / Physical
    html += '<div class="db-detail-section"><h3>Physical</h3>';
    html += detailRow('Spectral Type', obj.spectral_type || '—');
    html += detailRow('Luminosity Class', obj.luminosity_class || '—');
    if (obj.temperature_k) html += detailRow('Temperature', `${formatNumber(obj.temperature_k, 0)} K`);
    if (obj.mass_solar) html += detailRow('Mass', `${formatNumber(obj.mass_solar, 2)} M☉`);
    if (obj.radius_solar) html += detailRow('Radius', `${formatNumber(obj.radius_solar, 2)} R☉`);
    if (obj.luminosity_solar) html += detailRow('Luminosity', `${formatNumber(obj.luminosity_solar, 2)} L☉`);
    if (obj.angular_size_arcmin) html += detailRow('Angular Size', `${formatNumber(obj.angular_size_arcmin, 1)} arcmin`);
    if (obj.redshift) html += detailRow('Redshift', formatNumber(obj.redshift, 4));
    html += '</div>';

    // Notes
    if (obj.notes) {
      html += '<div class="db-detail-section db-detail-full"><h3>Notes</h3>';
      html += `<p class="db-notes">${escapeHtml(obj.notes)}</p></div>`;
    }

    html += '</div>';
    content.innerHTML = html;
  }

  function detailRow(label, value) {
    return `<div class="stat-row"><span class="stat-label">${label}</span><span class="stat-value">${value}</span></div>`;
  }

  function updateFavoriteButton(obj) {
    const btn = $('#btn-db-fav');
    if (!btn) return;
    const isFav = obj.is_favorite;
    btn.innerHTML = isFav ? 'Unfavorite' : 'Favorite';
    btn.className = `btn btn-sm ${isFav ? 'btn-fav' : 'btn-secondary'}`;
  }

  function closeDetail() {
    const detailCard = $('#card-db-detail');
    if (detailCard) detailCard.style.display = 'none';
    currentDetail = null;
  }

  // ─── Favorites ────────────────────────────────────────────────────────

  async function toggleFavorite(id) {
    try {
      // Find object in current list to know current state
      const obj = currentObjects.find(o => o.id === id || o.name === id);
      if (obj && obj.is_favorite) {
        await Api.removeFavorite(id);
        obj.is_favorite = false;
        App.showToast('Removed from favorites', 'info');
      } else {
        await Api.addFavorite(id);
        if (obj) obj.is_favorite = true;
        App.showToast('Added to favorites', 'success');
      }
      // Re-render list and detail
      renderObjectList();
      if (currentDetail && (currentDetail.id === id || currentDetail.name === id)) {
        currentDetail.is_favorite = !currentDetail.is_favorite;
        updateFavoriteButton(currentDetail);
      }
      loadStats(); // Refresh stats
    } catch (err) {
      App.showToast(`Favorite error: ${err.message}`, 'error');
    }
  }

  async function handleToggleFavorite() {
    if (!currentDetail) return;
    await toggleFavorite(currentDetail.id || currentDetail.name);
  }

  // ─── Create Object ────────────────────────────────────────────────────

  /** Helper: get trimmed string value or undefined */
  function getStr(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = el.value.trim();
    return v || undefined;
  }

  /** Helper: get parsed float or undefined */
  function getNum(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    // If the input has been enhanced with angle support, use the decimal getter
    if (el.getAngleDecimal) {
      const val = el.getAngleDecimal();
      return isFinite(val) ? val : undefined;
    }
    const v = el.value.trim();
    if (v === '') return undefined;
    const n = parseFloat(v);
    return isNaN(n) ? undefined : n;
  }

  /** Helper: get checkbox boolean */
  function getBool(id) {
    const el = document.getElementById(id);
    return el ? el.checked : false;
  }

  /** Helper: get tags/categories as array */
  function getStrArray(id) {
    const str = getStr(id);
    if (!str) return undefined;
    return str.split(',').map(s => s.trim()).filter(s => s.length > 0);
  }

  async function handleCreate(event) {
    event.preventDefault();

    const name = $('#db-new-name').value.trim();
    if (!name) {
      App.showToast('Object name is required', 'error');
      return;
    }

    const objectData = {
      name,
      object_type: $('#db-new-type').value || 'STAR',

      // Catalog & alternate names
      catalog_name: getStr('db-new-catalog-name'),
      alternate_names: getStr('db-new-alt-names'),

      // Coordinates
      ra_hours: getNum('db-new-ra'),
      dec_degrees: getNum('db-new-dec'),

      // Proper motion
      pm_ra: getNum('db-new-pm-ra'),
      pm_dec: getNum('db-new-pm-dec'),

      // Parallax & distance
      parallax_mas: getNum('db-new-parallax'),
      distance_pc: getNum('db-new-dist-pc'),
      distance_ly: getNum('db-new-dist-ly'),

      // Magnitudes
      v_magnitude: getNum('db-new-mag-v'),
      b_magnitude: getNum('db-new-mag-b'),
      j_magnitude: getNum('db-new-mag-j'),
      h_magnitude: getNum('db-new-mag-h'),
      k_magnitude: getNum('db-new-mag-k'),

      // Spectral classification
      spectral_type: getStr('db-new-spectral-type'),
      luminosity_class: getStr('db-new-lum-class'),

      // Physical parameters
      mass_solar: getNum('db-new-mass'),
      radius_solar: getNum('db-new-radius'),
      temperature_k: getNum('db-new-temp'),
      luminosity_solar: getNum('db-new-luminosity'),
      age_gyr: getNum('db-new-age'),

      // Solar system / extended physical
      diameter_km: getNum('db-new-diameter'),
      albedo: getNum('db-new-albedo'),
      rotation_period_hours: getNum('db-new-rotation'),
      angular_size_arcmin: getNum('db-new-angular-size'),
      redshift: getNum('db-new-redshift'),
      radial_velocity_kms: getNum('db-new-radial-vel'),
      apparent_dimensions_arcmin_x: getNum('db-new-app-dim-x'),
      apparent_dimensions_arcmin_y: getNum('db-new-app-dim-y'),

      // Orbital elements
      semi_major_axis_au: getNum('db-new-semi-major'),
      eccentricity: getNum('db-new-eccentricity'),
      inclination_deg: getNum('db-new-inclination'),
      longitude_asc_node_deg: getNum('db-new-long-node'),
      argument_perihelion_deg: getNum('db-new-arg-peri'),
      mean_anomaly_deg: getNum('db-new-mean-anom'),
      epoch_of_elements_jd: getNum('db-new-epoch-elem'),

      // Positional errors
      ra_error_mas: getNum('db-new-ra-error'),
      dec_error_mas: getNum('db-new-dec-error'),
      pm_ra_error: getNum('db-new-pm-ra-error'),
      pm_dec_error: getNum('db-new-pm-dec-error'),
      parallax_error: getNum('db-new-parallax-error'),

      // Catalog & source
      catalog_id: getStr('db-new-catalog-id'),
      catalog_version: getStr('db-new-catalog-ver'),
      data_source: getStr('db-new-data-source'),

      // Flags
      is_favorite: getBool('db-new-fav'),
      is_visible: getBool('db-new-visible'),
      ephemeris_available: getBool('db-new-ephemeris'),
      has_light_curve: getBool('db-new-light-curve'),
      has_spectrum: getBool('db-new-spectrum'),

      // Notes & metadata
      notes: getStr('db-new-notes'),
      user_notes: getStr('db-new-user-notes'),
      user_rating: getNum('db-new-user-rating'),
      created_by: getStr('db-new-created-by'),
      tags: getStrArray('db-new-tags'),
      categories: getStrArray('db-new-categories'),
    };

    // Constellation (stored in custom_fields)
    const constel = getStr('db-new-constellation');
    if (constel) {
      objectData.custom_fields = { constellation: constel.toUpperCase() };
    }

    // Remove undefined keys to keep payload clean
    Object.keys(objectData).forEach(key => {
      if (objectData[key] === undefined) {
        delete objectData[key];
      }
    });

    try {
      const result = await Api.createObject(objectData);
      App.showToast(`Object "${name}" created successfully`, 'success');
      // Reset form
      $('#db-create-form').reset();
      // Refresh list
      handleSearch();
      loadStats();
    } catch (err) {
      // Try to extract detailed error from proxy response
      App.showToast(`Create failed: ${err.message}`, 'error');
      // Log full error to console for debugging
      console.error('Create object error:', err);
    }
  }

  // ─── Slew to Object ──────────────────────────────────────────────────

  function handleSlewToDetail() {
    if (!currentDetail) return;

    const ra = currentDetail.ra_hours;
    const dec = currentDetail.dec_degrees;

    if (ra === undefined || ra === null || dec === undefined || dec === null) {
      App.showToast('Object has no coordinates', 'error');
      return;
    }

    // Fill slew form in Control tab and switch to it
    const raInput = $('#slew-ra');
    const decInput = $('#slew-dec');
    if (raInput && decInput) {
      raInput.value = ra;
      decInput.value = dec;
    }

    // Switch to Control tab
    const controlTab = document.querySelector('.tab-btn[data-tab="control"]');
    if (controlTab) controlTab.click();

    App.showToast(`Ready to slew to ${currentDetail.name || 'object'}`, 'success');
  }
// ─── Edit Object ──────────────────────────────────────────────────────

/** Pre-fill the edit form with data from an AstronomicalObject */
function populateEditForm(obj) {
  function setVal(id, val) {
    const el = document.getElementById(id);
    if (!el) return;
    if (el.type === 'checkbox') {
      el.checked = !!val;
    } else if (el.setAngleDecimal && typeof val === 'number' && isFinite(val)) {
      // Enhanced angle input: set decimal value (displays as DMS/HMS)
      el.setAngleDecimal(val);
    } else {
      el.value = (val !== undefined && val !== null) ? String(val) : '';
    }
  }

  setVal('db-edit-name', obj.name);
  setVal('db-edit-type', obj.object_type || 'STAR');
  setVal('db-edit-catalog-name', obj.catalog_name);
  setVal('db-edit-alt-names', obj.alternate_names);

  setVal('db-edit-ra', obj.ra_hours);
  setVal('db-edit-dec', obj.dec_degrees);
  setVal('db-edit-pm-ra', obj.pm_ra);
  setVal('db-edit-pm-dec', obj.pm_dec);
  setVal('db-edit-parallax', obj.parallax_mas);
  setVal('db-edit-dist-pc', obj.distance_pc);
  setVal('db-edit-dist-ly', obj.distance_ly);

  setVal('db-edit-mag-v', obj.v_magnitude);
  setVal('db-edit-mag-b', obj.b_magnitude);
  setVal('db-edit-mag-j', obj.j_magnitude);
  setVal('db-edit-mag-h', obj.h_magnitude);
  setVal('db-edit-mag-k', obj.k_magnitude);

  setVal('db-edit-spectral-type', obj.spectral_type);
  setVal('db-edit-lum-class', obj.luminosity_class);

  setVal('db-edit-mass', obj.mass_solar);
  setVal('db-edit-radius', obj.radius_solar);
  setVal('db-edit-temp', obj.temperature_k);
  setVal('db-edit-luminosity', obj.luminosity_solar);
  setVal('db-edit-age', obj.age_gyr);

  setVal('db-edit-diameter', obj.diameter_km);
  setVal('db-edit-albedo', obj.albedo);
  setVal('db-edit-rotation', obj.rotation_period_hours);
  setVal('db-edit-angular-size', obj.angular_size_arcmin);
  setVal('db-edit-redshift', obj.redshift);
  setVal('db-edit-radial-vel', obj.radial_velocity_kms);
  setVal('db-edit-app-dim-x', obj.apparent_dimensions_arcmin_x);
  setVal('db-edit-app-dim-y', obj.apparent_dimensions_arcmin_y);

  setVal('db-edit-semi-major', obj.semi_major_axis_au);
  setVal('db-edit-eccentricity', obj.eccentricity);
  setVal('db-edit-inclination', obj.inclination_deg);
  setVal('db-edit-long-node', obj.longitude_asc_node_deg);
  setVal('db-edit-arg-peri', obj.argument_perihelion_deg);
  setVal('db-edit-mean-anom', obj.mean_anomaly_deg);
  setVal('db-edit-epoch-elem', obj.epoch_of_elements_jd);

  setVal('db-edit-ra-error', obj.ra_error_mas);
  setVal('db-edit-dec-error', obj.dec_error_mas);
  setVal('db-edit-pm-ra-error', obj.pm_ra_error);
  setVal('db-edit-pm-dec-error', obj.pm_dec_error);
  setVal('db-edit-parallax-error', obj.parallax_error);

  setVal('db-edit-catalog-id', obj.catalog_id);
  setVal('db-edit-catalog-ver', obj.catalog_version);
  setVal('db-edit-data-source', obj.data_source);

  setVal('db-edit-fav', obj.is_favorite);
  setVal('db-edit-visible', obj.is_visible);
  setVal('db-edit-ephemeris', obj.ephemeris_available);
  setVal('db-edit-light-curve', obj.has_light_curve);
  setVal('db-edit-spectrum', obj.has_spectrum);

  setVal('db-edit-notes', obj.notes);
  setVal('db-edit-user-notes', obj.user_notes);
  setVal('db-edit-user-rating', obj.user_rating);
  setVal('db-edit-created-by', obj.created_by);

  // Constellation (stored in custom_fields)
  const constel = obj.custom_fields && obj.custom_fields.constellation;
  if (constel) {
    setVal('db-edit-constellation', constel);
  }

  // Arrays → comma-separated
  if (obj.tags && Array.isArray(obj.tags) && obj.tags.length > 0) {
    setVal('db-edit-tags', obj.tags.join(', '));
  }
  if (obj.categories && Array.isArray(obj.categories) && obj.categories.length > 0) {
    setVal('db-edit-categories', obj.categories.join(', '));
  }
}

function handleEditObject() {
  if (!currentDetail) return;

  const title = $('#db-edit-title');
  if (title) title.textContent = I18n.t('db.edit_title_with_name', { name: currentDetail.name || I18n.t('db.unnamed') });

  populateEditForm(currentDetail);

  // Switch cards: hide detail, show edit
  const detailCard = $('#card-db-detail');
  const editCard = $('#card-db-edit');
  if (detailCard) detailCard.style.display = 'none';
  if (editCard) editCard.style.display = 'block';
}

function handleCancelEdit() {
  const editCard = $('#card-db-edit');
  const detailCard = $('#card-db-detail');
  if (editCard) editCard.style.display = 'none';
  // Only show detail if there's a current object
  if (currentDetail) {
    if (detailCard) detailCard.style.display = 'block';
  }
}

/** Collect all fields from the edit form into an object for the API */
function collectEditData() {
  function getStr(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    const v = el.value.trim();
    return v || undefined;
  }
  function getNum(id) {
    const el = document.getElementById(id);
    if (!el) return undefined;
    // If the input has been enhanced with angle support, use the decimal getter
    if (el.getAngleDecimal) {
      const val = el.getAngleDecimal();
      return isFinite(val) ? val : undefined;
    }
    const v = el.value.trim();
    if (v === '') return undefined;
    const n = parseFloat(v);
    return isNaN(n) ? undefined : n;
  }
  function getBool(id) {
    const el = document.getElementById(id);
    return el ? el.checked : false;
  }
  function getStrArray(id) {
    const str = getStr(id);
    if (!str) return undefined;
    return str.split(',').map(s => s.trim()).filter(s => s.length > 0);
  }

  const data = {
    name: getStr('db-edit-name') || '',
    object_type: getStr('db-edit-type') || 'STAR',
    catalog_name: getStr('db-edit-catalog-name'),
    alternate_names: getStr('db-edit-alt-names'),

    ra_hours: getNum('db-edit-ra'),
    dec_degrees: getNum('db-edit-dec'),
    pm_ra: getNum('db-edit-pm-ra'),
    pm_dec: getNum('db-edit-pm-dec'),
    parallax_mas: getNum('db-edit-parallax'),
    distance_pc: getNum('db-edit-dist-pc'),
    distance_ly: getNum('db-edit-dist-ly'),

    v_magnitude: getNum('db-edit-mag-v'),
    b_magnitude: getNum('db-edit-mag-b'),
    j_magnitude: getNum('db-edit-mag-j'),
    h_magnitude: getNum('db-edit-mag-h'),
    k_magnitude: getNum('db-edit-mag-k'),

    spectral_type: getStr('db-edit-spectral-type'),
    luminosity_class: getStr('db-edit-lum-class'),

    mass_solar: getNum('db-edit-mass'),
    radius_solar: getNum('db-edit-radius'),
    temperature_k: getNum('db-edit-temp'),
    luminosity_solar: getNum('db-edit-luminosity'),
    age_gyr: getNum('db-edit-age'),

    diameter_km: getNum('db-edit-diameter'),
    albedo: getNum('db-edit-albedo'),
    rotation_period_hours: getNum('db-edit-rotation'),
    angular_size_arcmin: getNum('db-edit-angular-size'),
    redshift: getNum('db-edit-redshift'),
    radial_velocity_kms: getNum('db-edit-radial-vel'),
    apparent_dimensions_arcmin_x: getNum('db-edit-app-dim-x'),
    apparent_dimensions_arcmin_y: getNum('db-edit-app-dim-y'),

    semi_major_axis_au: getNum('db-edit-semi-major'),
    eccentricity: getNum('db-edit-eccentricity'),
    inclination_deg: getNum('db-edit-inclination'),
    longitude_asc_node_deg: getNum('db-edit-long-node'),
    argument_perihelion_deg: getNum('db-edit-arg-peri'),
    mean_anomaly_deg: getNum('db-edit-mean-anom'),
    epoch_of_elements_jd: getNum('db-edit-epoch-elem'),

    ra_error_mas: getNum('db-edit-ra-error'),
    dec_error_mas: getNum('db-edit-dec-error'),
    pm_ra_error: getNum('db-edit-pm-ra-error'),
    pm_dec_error: getNum('db-edit-pm-dec-error'),
    parallax_error: getNum('db-edit-parallax-error'),

    catalog_id: getStr('db-edit-catalog-id'),
    catalog_version: getStr('db-edit-catalog-ver'),
    data_source: getStr('db-edit-data-source'),

    is_favorite: getBool('db-edit-fav'),
    is_visible: getBool('db-edit-visible'),
    ephemeris_available: getBool('db-edit-ephemeris'),
    has_light_curve: getBool('db-edit-light-curve'),
    has_spectrum: getBool('db-edit-spectrum'),

    notes: getStr('db-edit-notes'),
    user_notes: getStr('db-edit-user-notes'),
    user_rating: getNum('db-edit-user-rating'),
    created_by: getStr('db-edit-created-by'),
    tags: getStrArray('db-edit-tags'),
    categories: getStrArray('db-edit-categories'),
  };

  // Constellation (stored in custom_fields)
  const constel = getStr('db-edit-constellation');
  if (constel) {
    data.custom_fields = { constellation: constel.toUpperCase() };
  }

  return data;
}

async function handleUpdate(event) {
  event.preventDefault();

  if (!currentDetail) {
    App.showToast('No object selected for editing', 'error');
    return;
  }

  const id = currentDetail.id || currentDetail.name;
  const name = $('#db-edit-name').value.trim();
  if (!name) {
    App.showToast('Object name is required', 'error');
    return;
  }

  const objectData = collectEditData();

  // Remove undefined keys to keep payload clean
  Object.keys(objectData).forEach(key => {
    if (objectData[key] === undefined) {
      delete objectData[key];
    }
  });

  try {
    await Api.updateObject(id, objectData);
    App.showToast(`Object "${name}" updated successfully`, 'success');

    // Refresh current detail by re-fetching
    try {
      currentDetail = await Api.getObject(id);
    } catch (_) { /* ignore */ }

    // Switch back to detail view
    handleCancelEdit();
    if (currentDetail) {
      renderDetail(currentDetail);
      updateFavoriteButton(currentDetail);
    }
    handleSearch();
    loadStats();
  } catch (err) {
    App.showToast(`Update failed: ${err.message}`, 'error');
    console.error('Update object error:', err);
  }
}

// ─── Delete Object ────────────────────────────────────────────────────


  async function handleDeleteObject() {
    if (!currentDetail) return;
    const name = currentDetail.name || 'this object';
    if (!confirm(`Delete "${name}"? This cannot be undone.`)) return;

    try {
      await Api.deleteObject(currentDetail.id || currentDetail.name);
      App.showToast(`"${name}" deleted`, 'info');
      closeDetail();
      handleSearch();
      loadStats();
    } catch (err) {
      App.showToast(`Delete failed: ${err.message}`, 'error');
    }
  }

  // ─── Collapse/Expand ──────────────────────────────────────────────────

  /**
   * Toggle the "Add New Object" panel collapse state.
   */
  function toggleCreatePanel() {
    const card = $('#card-db-create');
    const btn = $('#btn-db-create-toggle');
    if (!card) return;
    const collapsed = card.classList.toggle('card-collapsed');
    if (btn) btn.textContent = collapsed ? '+' : '\u2212';
  }

  /**
   * Toggle the "Database Stats" panel collapse state.
   */
  function toggleStatsPanel() {
    const card = $('#card-db-stats');
    const btn = $('#btn-db-stats-toggle');
    if (!card) return;
    const collapsed = card.classList.toggle('card-collapsed');
    if (btn) btn.textContent = collapsed ? '+' : '\u2212';
  }

  // ─── Import: Toggle Panel ────────────────────────────────────────────

  function toggleImportPanel() {
    const card = $('#card-db-import');
    const btn = $('#btn-db-import-toggle');
    if (!card) return;
    const collapsed = card.classList.toggle('card-collapsed');
    // Load presets when first expanded
    if (!card.classList.contains('card-collapsed')) {
      loadImportPresets();
    }
    if (btn) btn.textContent = collapsed ? '+' : '\u2212';
  }

  // ─── Import: Load Presets ────────────────────────────────────────────

  async function loadImportPresets() {
    const container = $('#db-import-presets');
    if (!container || container.dataset.loaded) return;
    try {
      container.innerHTML = '<div class="status-placeholder">Loading presets...</div>';
      const result = await Api.getImportPresets();
      container.dataset.loaded = 'true';
      if (!result || !result.presets || result.presets.length === 0) {
        container.innerHTML = '<div class="status-placeholder">No presets available.</div>';
        return;
      }
      container.innerHTML = '';
      result.presets.forEach(preset => {
        const btn = document.createElement('button');
        btn.className = 'preset-btn';
        btn.dataset.preset = preset.name;
        btn.title = `${preset.description}\nFormat: ${preset.format}\nObjects: ~${preset.size.toLocaleString()}`;
        btn.innerHTML = `
          <span class="preset-btn-name">${escapeHtml(preset.label)}</span>
          <span class="preset-btn-info">${preset.type} · ~${preset.size.toLocaleString()} objects</span>
        `;
        btn.addEventListener('click', () => handlePresetImport(preset.name));
        container.appendChild(btn);
      });
    } catch (err) {
      container.innerHTML = `<div class="status-placeholder">Failed to load presets: ${escapeHtml(err.message)}</div>`;
    }
  }

  // ─── Import: Handle File Import ──────────────────────────────────────

  async function handleFileImport() {
    const statusEl = $('#db-import-file-status');
    const fileInput = $('#db-import-file');
    const formatSelect = $('#db-import-format');
    const catalogName = $('#db-import-catalog-name');
    const overwriteCheck = $('#db-import-overwrite');

    if (!fileInput || !fileInput.files || fileInput.files.length === 0) {
      if (statusEl) statusEl.textContent = '⚠️ Please select a file first.';
      return;
    }

    const file = fileInput.files[0];
    const format = formatSelect ? formatSelect.value : 'CSV';
    const name = catalogName ? catalogName.value.trim() || file.name.replace(/\.[^/.]+$/, '') : file.name.replace(/\.[^/.]+$/, '');
    const overwrite = overwriteCheck ? overwriteCheck.checked : false;

    if (statusEl) statusEl.textContent = '⏳ Reading file...';

    try {
      const text = await file.text();
      if (statusEl) statusEl.textContent = '⏳ Importing...';

      const result = await Api.importCatalog(format, text, {
        catalog_name: name,
        overwrite,
      });

      showImportResults(result);
      if (statusEl) statusEl.textContent = '✅ Import complete!';
      // Refresh stats and list
      loadStats();
    } catch (err) {
      if (statusEl) statusEl.textContent = `❌ Import failed: ${err.message}`;
      console.error('[DB Import] File import error:', err);
    }
  }

  // ─── Import: Handle URL Import ───────────────────────────────────────

  async function handleUrlImport() {
    const statusEl = $('#db-import-url-status');
    const urlInput = $('#db-import-url');
    const formatSelect = $('#db-import-url-format');
    const nameInput = $('#db-import-url-name');
    const overwriteCheck = $('#db-import-url-overwrite');

    const url = urlInput ? urlInput.value.trim() : '';
    if (!url) {
      if (statusEl) statusEl.textContent = '⚠️ Please enter a URL.';
      return;
    }

    const format = formatSelect ? formatSelect.value : 'CSV';
    const name = nameInput ? nameInput.value.trim() || 'remote_catalog' : 'remote_catalog';
    const overwrite = overwriteCheck ? overwriteCheck.checked : false;

    if (statusEl) statusEl.textContent = '⏳ Fetching remote data...';

    try {
      const result = await Api.importCatalogFromUrl(url, format, {
        catalog_name: name,
        overwrite,
      });

      showImportResults(result);
      if (statusEl) statusEl.textContent = '✅ Import complete!';
      loadStats();
    } catch (err) {
      if (statusEl) statusEl.textContent = `❌ Import failed: ${err.message}`;
      console.error('[DB Import] URL import error:', err);
    }
  }

  // ─── Import: Handle Preset Import ────────────────────────────────────

  async function handlePresetImport(presetName) {
    const statusEl = $('#db-import-preset-status');
    if (!statusEl) return;

    // Disable all preset buttons during import
    const presetBtns = document.querySelectorAll('.preset-btn');
    presetBtns.forEach(btn => btn.disabled = true);

    statusEl.innerHTML = `⏳ Importing <strong>${escapeHtml(presetName)}</strong> catalog... This may take a while.`;

    try {
      const result = await Api.importPreset(presetName, { overwrite: false });
      showImportResults(result);
      statusEl.innerHTML = `✅ <strong>${escapeHtml(presetName)}</strong> catalog imported successfully!`;
      loadStats();
    } catch (err) {
      statusEl.innerHTML = `❌ Import of <strong>${escapeHtml(presetName)}</strong> failed: ${escapeHtml(err.message)}`;
      console.error('[DB Import] Preset import error:', err);
    } finally {
      presetBtns.forEach(btn => btn.disabled = false);
    }
  }

  // ─── Import: Display Results ─────────────────────────────────────────

  function showImportResults(result) {
    const container = $('#db-import-results');
    const content = $('#db-import-results-content');
    if (!container || !content) return;

    if (!result) {
      container.style.display = 'none';
      return;
    }

    container.style.display = 'block';

    const imported = result.objects_imported || 0;
    const skipped = result.objects_skipped || 0;
    const updated = result.objects_updated || 0;
    const errors = result.errors || [];
    let importTime = result.import_time || 0;
    if (importTime && typeof importTime === 'object' && importTime.seconds != null) {
      importTime = Number(importTime.seconds) + (Number(importTime.nanos) || 0) / 1e9;
    }

    let html = `
      <div class="import-result-stats">
        <span class="import-result-stat import-result-ok">✅ Imported: <strong>${imported}</strong></span>
        <span class="import-result-stat import-result-warn">⏭️ Skipped: <strong>${skipped}</strong></span>
        <span class="import-result-stat import-result-info">🔄 Updated: <strong>${updated}</strong></span>
        ${importTime ? `<span class="import-result-stat import-result-time">⏱️ Time: <strong>${importTime.toFixed(2)}s</strong></span>` : ''}
      </div>
    `;

    if (errors.length > 0) {
      html += `<details class="import-errors" style="margin-top:8px;">
        <summary style="cursor:pointer; color:var(--error-color, #f66);">Errors (${errors.length})</summary>
        <ul style="margin:4px 0 0 16px; font-size:0.85em;">
          ${errors.map(e => `<li>${escapeHtml(e)}</li>`).join('')}
        </ul>
      </details>`;
    }

    content.innerHTML = html;
  }

  // ─── Utilities ────────────────────────────────────────────────────────

  function formatTypeName(type) {
    if (!type) return 'Unknown';
    return type
      .replace(/_/g, ' ')
      .split(' ')
      .map(w => w.charAt(0).toUpperCase() + w.slice(1).toLowerCase())
      .join(' ');
  }

  function escapeHtml(str) {
    if (!str) return '';
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
  }

  // ─── Public API ───────────────────────────────────────────────────────

  return {
    init,
    loadStats,
    selectObject,
    toggleFavorite,
  };
})();
