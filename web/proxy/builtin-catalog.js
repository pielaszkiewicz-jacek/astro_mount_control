/**
 * Built-in Astronomical Catalog
 *
 * Provides a fallback in-memory catalog of popular deep-sky objects
 * when the gRPC database service is unavailable.
 *
 * Contains: Messier, NGC, IC, Caldwell, bright stars, solar system
 */
'use strict';

// ─── Built-in Objects ────────────────────────────────────────────────────────

const OBJECTS = [
  // ── Messier Catalog ──
  { name: 'M1', alt_names: 'Crab Nebula', type: 'SUPERNOVA_REMNANT', ra: 5.583, dec: 22.017, magnitude: 8.4, catalog: 'Messier', constellation: 'Tau' },
  { name: 'M31', alt_names: 'Andromeda Galaxy', type: 'GALAXY_SPIRAL', ra: 0.712, dec: 41.269, magnitude: 3.4, catalog: 'Messier', constellation: 'And' },
  { name: 'M42', alt_names: 'Orion Nebula', type: 'DIFFUSE_NEBULA', ra: 5.583, dec: -5.383, magnitude: 4.0, catalog: 'Messier', constellation: 'Ori' },
  { name: 'M45', alt_names: 'Pleiades', type: 'STAR_CLUSTER_OPEN', ra: 3.783, dec: 24.117, magnitude: 1.6, catalog: 'Messier', constellation: 'Tau' },
  { name: 'M51', alt_names: 'Whirlpool Galaxy', type: 'GALAXY_SPIRAL', ra: 13.500, dec: 47.233, magnitude: 8.4, catalog: 'Messier', constellation: 'CVn' },
  { name: 'M57', alt_names: 'Ring Nebula', type: 'PLANETARY_NEBULA', ra: 18.883, dec: 33.033, magnitude: 8.8, catalog: 'Messier', constellation: 'Lyr' },
  { name: 'M81', alt_names: 'Bode\'s Galaxy', type: 'GALAXY_SPIRAL', ra: 9.933, dec: 69.067, magnitude: 6.9, catalog: 'Messier', constellation: 'UMa' },
  { name: 'M82', alt_names: 'Cigar Galaxy', type: 'GALAXY_IRREGULAR', ra: 9.933, dec: 69.683, magnitude: 8.4, catalog: 'Messier', constellation: 'UMa' },
  { name: 'M101', alt_names: 'Pinwheel Galaxy', type: 'GALAXY_SPIRAL', ra: 14.050, dec: 54.350, magnitude: 7.9, catalog: 'Messier', constellation: 'UMa' },
  { name: 'M104', alt_names: 'Sombrero Galaxy', type: 'GALAXY_SPIRAL', ra: 12.667, dec: -11.617, magnitude: 8.0, catalog: 'Messier', constellation: 'Vir' },
  { name: 'M13', alt_names: 'Hercules Cluster', type: 'STAR_CLUSTER_GLOBULAR', ra: 16.700, dec: 36.450, magnitude: 5.8, catalog: 'Messier', constellation: 'Her' },
  { name: 'M27', alt_names: 'Dumbbell Nebula', type: 'PLANETARY_NEBULA', ra: 19.983, dec: 22.717, magnitude: 7.4, catalog: 'Messier', constellation: 'Vul' },
  { name: 'M92', alt_names: '', type: 'STAR_CLUSTER_GLOBULAR', ra: 17.283, dec: 43.133, magnitude: 6.3, catalog: 'Messier', constellation: 'Her' },
  { name: 'M3', alt_names: '', type: 'STAR_CLUSTER_GLOBULAR', ra: 13.700, dec: 28.383, magnitude: 6.2, catalog: 'Messier', constellation: 'CVn' },
  { name: 'M35', alt_names: '', type: 'STAR_CLUSTER_OPEN', ra: 6.133, dec: 24.350, magnitude: 5.1, catalog: 'Messier', constellation: 'Gem' },
  { name: 'M36', alt_names: '', type: 'STAR_CLUSTER_OPEN', ra: 5.600, dec: 34.133, magnitude: 6.0, catalog: 'Messier', constellation: 'Aur' },
  { name: 'M37', alt_names: '', type: 'STAR_CLUSTER_OPEN', ra: 5.883, dec: 32.550, magnitude: 5.6, catalog: 'Messier', constellation: 'Aur' },
  { name: 'M38', alt_names: '', type: 'STAR_CLUSTER_OPEN', ra: 5.467, dec: 35.833, magnitude: 6.4, catalog: 'Messier', constellation: 'Aur' },
  { name: 'M44', alt_names: 'Beehive Cluster', type: 'STAR_CLUSTER_OPEN', ra: 8.667, dec: 19.667, magnitude: 3.1, catalog: 'Messier', constellation: 'Cnc' },
  { name: 'M65', alt_names: '', type: 'GALAXY_SPIRAL', ra: 11.317, dec: 13.083, magnitude: 9.3, catalog: 'Messier', constellation: 'Leo' },
  { name: 'M66', alt_names: '', type: 'GALAXY_SPIRAL', ra: 11.333, dec: 12.983, magnitude: 8.9, catalog: 'Messier', constellation: 'Leo' },
  { name: 'M87', alt_names: 'Virgo A', type: 'GALAXY_ELLIPTICAL', ra: 12.517, dec: 12.400, magnitude: 8.6, catalog: 'Messier', constellation: 'Vir' },
  { name: 'M97', alt_names: 'Owl Nebula', type: 'PLANETARY_NEBULA', ra: 11.233, dec: 55.017, magnitude: 9.9, catalog: 'Messier', constellation: 'UMa' },
  { name: 'M110', alt_names: '', type: 'GALAXY_ELLIPTICAL', ra: 0.667, dec: 41.683, magnitude: 8.5, catalog: 'Messier', constellation: 'And' },
  { name: 'M33', alt_names: 'Triangulum Galaxy', type: 'GALAXY_SPIRAL', ra: 1.567, dec: 30.650, magnitude: 5.7, catalog: 'Messier', constellation: 'Tri' },
  { name: 'M74', alt_names: '', type: 'GALAXY_SPIRAL', ra: 1.600, dec: 15.783, magnitude: 9.4, catalog: 'Messier', constellation: 'Psc' },
  { name: 'M77', alt_names: '', type: 'GALAXY_SPIRAL', ra: 2.700, dec: -0.017, magnitude: 8.6, catalog: 'Messier', constellation: 'Cet' },
  { name: 'M78', alt_names: '', type: 'DIFFUSE_NEBULA', ra: 5.767, dec: 0.033, magnitude: 8.3, catalog: 'Messier', constellation: 'Ori' },
  { name: 'M8', alt_names: 'Lagoon Nebula', type: 'DIFFUSE_NEBULA', ra: 18.050, dec: -24.383, magnitude: 6.0, catalog: 'Messier', constellation: 'Sgr' },
  { name: 'M16', alt_names: 'Eagle Nebula', type: 'EMISSION_NEBULA', ra: 18.317, dec: -13.800, magnitude: 6.4, catalog: 'Messier', constellation: 'Ser' },
  { name: 'M17', alt_names: 'Omega Nebula', type: 'EMISSION_NEBULA', ra: 18.333, dec: -16.167, magnitude: 7.0, catalog: 'Messier', constellation: 'Sgr' },
  { name: 'M20', alt_names: 'Trifid Nebula', type: 'DIFFUSE_NEBULA', ra: 18.033, dec: -23.017, magnitude: 9.0, catalog: 'Messier', constellation: 'Sgr' },
  { name: 'M22', alt_names: '', type: 'STAR_CLUSTER_GLOBULAR', ra: 18.600, dec: -23.900, magnitude: 5.1, catalog: 'Messier', constellation: 'Sgr' },

  // ── NGC Catalog (selected) ──
  { name: 'NGC 7000', alt_names: 'North America Nebula', type: 'DIFFUSE_NEBULA', ra: 20.967, dec: 44.517, magnitude: 4.0, catalog: 'NGC', constellation: 'Cyg' },
  { name: 'NGC 6960', alt_names: 'Veil Nebula (West)', type: 'SUPERNOVA_REMNANT', ra: 20.750, dec: 30.583, magnitude: 7.0, catalog: 'NGC', constellation: 'Cyg' },
  { name: 'NGC 6992', alt_names: 'Veil Nebula (East)', type: 'SUPERNOVA_REMNANT', ra: 20.933, dec: 31.700, magnitude: 7.0, catalog: 'NGC', constellation: 'Cyg' },
  { name: 'NGC 6543', alt_names: 'Cat\'s Eye Nebula', type: 'PLANETARY_NEBULA', ra: 17.967, dec: 66.633, magnitude: 9.8, catalog: 'NGC', constellation: 'Dra' },
  { name: 'NGC 2392', alt_names: 'Eskimo Nebula', type: 'PLANETARY_NEBULA', ra: 7.467, dec: 20.917, magnitude: 10.0, catalog: 'NGC', constellation: 'Gem' },
  { name: 'NGC 3242', alt_names: 'Ghost of Jupiter', type: 'PLANETARY_NEBULA', ra: 10.417, dec: -18.633, magnitude: 8.6, catalog: 'NGC', constellation: 'Hya' },
  { name: 'NGC 6826', alt_names: 'Blinking Planetary', type: 'PLANETARY_NEBULA', ra: 19.733, dec: 50.517, magnitude: 8.8, catalog: 'NGC', constellation: 'Cyg' },
  { name: 'NGC 7662', alt_names: 'Blue Snowball', type: 'PLANETARY_NEBULA', ra: 23.433, dec: 42.533, magnitude: 8.6, catalog: 'NGC', constellation: 'And' },
  { name: 'NGC 869', alt_names: 'Double Cluster (NGC 869)', type: 'STAR_CLUSTER_OPEN', ra: 2.317, dec: 57.133, magnitude: 3.7, catalog: 'NGC', constellation: 'Per' },
  { name: 'NGC 884', alt_names: 'Double Cluster (NGC 884)', type: 'STAR_CLUSTER_OPEN', ra: 2.350, dec: 57.133, magnitude: 3.8, catalog: 'NGC', constellation: 'Per' },
  { name: 'NGC 752', alt_names: '', type: 'STAR_CLUSTER_OPEN', ra: 1.933, dec: 37.700, magnitude: 5.7, catalog: 'NGC', constellation: 'And' },
  { name: 'NGC 457', alt_names: 'Owl Cluster', type: 'STAR_CLUSTER_OPEN', ra: 1.317, dec: 58.267, magnitude: 6.4, catalog: 'NGC', constellation: 'Cas' },
  { name: 'NGC 7789', alt_names: 'White Rose Cluster', type: 'STAR_CLUSTER_OPEN', ra: 23.950, dec: 56.717, magnitude: 6.7, catalog: 'NGC', constellation: 'Cas' },
  { name: 'NGC 4565', alt_names: 'Needle Galaxy', type: 'GALAXY_SPIRAL', ra: 12.600, dec: 25.983, magnitude: 9.6, catalog: 'NGC', constellation: 'Com' },
  { name: 'NGC 6946', alt_names: 'Fireworks Galaxy', type: 'GALAXY_SPIRAL', ra: 20.567, dec: 60.150, magnitude: 9.0, catalog: 'NGC', constellation: 'Cep' },
  { name: 'NGC 7331', alt_names: '', type: 'GALAXY_SPIRAL', ra: 22.617, dec: 34.417, magnitude: 9.5, catalog: 'NGC', constellation: 'Peg' },

  // ── Caldwell Catalog (selected) ──
  { name: 'Caldwell 1', alt_names: 'NGC 188', type: 'STAR_CLUSTER_OPEN', ra: 0.783, dec: 85.250, magnitude: 8.1, catalog: 'Caldwell', constellation: 'Cep' },
  { name: 'Caldwell 2', alt_names: 'NGC 40', type: 'PLANETARY_NEBULA', ra: 0.217, dec: 72.517, magnitude: 11.4, catalog: 'Caldwell', constellation: 'Cep' },
  { name: 'Caldwell 3', alt_names: 'NGC 4236', type: 'GALAXY_IRREGULAR', ra: 12.283, dec: 69.467, magnitude: 9.6, catalog: 'Caldwell', constellation: 'Dra' },
  { name: 'Caldwell 4', alt_names: 'NGC 7023', type: 'DIFFUSE_NEBULA', ra: 21.017, dec: 68.167, magnitude: 7.0, catalog: 'Caldwell', constellation: 'Cep' },
  { name: 'Caldwell 5', alt_names: 'IC 342', type: 'GALAXY_SPIRAL', ra: 3.717, dec: 68.100, magnitude: 9.1, catalog: 'Caldwell', constellation: 'Cam' },

  // ── Bright Stars ──
  { name: 'Sirius', alt_names: 'α CMa', type: 'STAR', ra: 6.750, dec: -16.717, magnitude: -1.46, catalog: 'Bright Star', constellation: 'CMa' },
  { name: 'Vega', alt_names: 'α Lyr', type: 'STAR', ra: 18.617, dec: 38.783, magnitude: 0.03, catalog: 'Bright Star', constellation: 'Lyr' },
  { name: 'Arcturus', alt_names: 'α Boo', type: 'STAR', ra: 14.267, dec: 19.183, magnitude: -0.05, catalog: 'Bright Star', constellation: 'Boo' },
  { name: 'Capella', alt_names: 'α Aur', type: 'STAR', ra: 5.283, dec: 45.983, magnitude: 0.08, catalog: 'Bright Star', constellation: 'Aur' },
  { name: 'Rigel', alt_names: 'β Ori', type: 'STAR', ra: 5.233, dec: -8.200, magnitude: 0.18, catalog: 'Bright Star', constellation: 'Ori' },
  { name: 'Betelgeuse', alt_names: 'α Ori', type: 'STAR', ra: 5.917, dec: 7.400, magnitude: 0.45, catalog: 'Bright Star', constellation: 'Ori' },
  { name: 'Altair', alt_names: 'α Aql', type: 'STAR', ra: 19.833, dec: 8.867, magnitude: 0.76, catalog: 'Bright Star', constellation: 'Aql' },
  { name: 'Aldebaran', alt_names: 'α Tau', type: 'STAR', ra: 4.600, dec: 16.500, magnitude: 0.87, catalog: 'Bright Star', constellation: 'Tau' },
  { name: 'Antares', alt_names: 'α Sco', type: 'STAR', ra: 16.483, dec: -26.417, magnitude: 0.96, catalog: 'Bright Star', constellation: 'Sco' },
  { name: 'Spica', alt_names: 'α Vir', type: 'STAR', ra: 13.417, dec: -11.150, magnitude: 0.98, catalog: 'Bright Star', constellation: 'Vir' },
  { name: 'Pollux', alt_names: 'β Gem', type: 'STAR', ra: 7.750, dec: 28.017, magnitude: 1.14, catalog: 'Bright Star', constellation: 'Gem' },
  { name: 'Fomalhaut', alt_names: 'α PsA', type: 'STAR', ra: 22.950, dec: -29.617, magnitude: 1.16, catalog: 'Bright Star', constellation: 'PsA' },
  { name: 'Deneb', alt_names: 'α Cyg', type: 'STAR', ra: 20.683, dec: 45.283, magnitude: 1.25, catalog: 'Bright Star', constellation: 'Cyg' },
  { name: 'Regulus', alt_names: 'α Leo', type: 'STAR', ra: 10.133, dec: 11.967, magnitude: 1.36, catalog: 'Bright Star', constellation: 'Leo' },
  { name: 'Polaris', alt_names: 'α UMi', type: 'STAR', ra: 2.517, dec: 89.267, magnitude: 1.98, catalog: 'Bright Star', constellation: 'UMi' },

  // ── Solar System ──
  { name: 'Jupiter', alt_names: '', type: 'PLANET', ra: 0.000, dec: 0.000, magnitude: -2.7, catalog: 'Solar System', constellation: '' },
  { name: 'Saturn', alt_names: '', type: 'PLANET', ra: 0.000, dec: 0.000, magnitude: 0.7, catalog: 'Solar System', constellation: '' },
  { name: 'Mars', alt_names: '', type: 'PLANET', ra: 0.000, dec: 0.000, magnitude: -2.0, catalog: 'Solar System', constellation: '' },
  { name: 'Venus', alt_names: '', type: 'PLANET', ra: 0.000, dec: 0.000, magnitude: -4.6, catalog: 'Solar System', constellation: '' },
];

// ─── Known Type Mappings ─────────────────────────────────────────────────────

const TYPE_MAP = {
  'STAR': 'STAR',
  'DOUBLE_STAR': 'DOUBLE_STAR',
  'VARIABLE_STAR': 'VARIABLE_STAR',
  'STAR_CLUSTER_OPEN': 'STAR_CLUSTER_OPEN',
  'STAR_CLUSTER_GLOBULAR': 'STAR_CLUSTER_GLOBULAR',
  'PLANETARY_NEBULA': 'PLANETARY_NEBULA',
  'DIFFUSE_NEBULA': 'DIFFUSE_NEBULA',
  'EMISSION_NEBULA': 'EMISSION_NEBULA',
  'DARK_NEBULA': 'DARK_NEBULA',
  'REFLECTION_NEBULA': 'REFLECTION_NEBULA',
  'GALAXY_SPIRAL': 'GALAXY_SPIRAL',
  'GALAXY_ELLIPTICAL': 'GALAXY_ELLIPTICAL',
  'GALAXY_IRREGULAR': 'GALAXY_IRREGULAR',
  'GALAXY_LENTICULAR': 'GALAXY_LENTICULAR',
  'GALAXY': 'GALAXY_SPIRAL',
  'QUASAR': 'QUASAR',
  'SUPERNOVA_REMNANT': 'SUPERNOVA_REMNANT',
  'PLANET': 'PLANET',
  'COMET': 'COMET',
  'ASTEROID': 'ASTEROID',
  'NEBULA': 'DIFFUSE_NEBULA',
  'GLOBULAR_CLUSTER': 'STAR_CLUSTER_GLOBULAR',
  'OPEN_CLUSTER': 'STAR_CLUSTER_OPEN',
  'Gx': 'GALAXY_SPIRAL',
  'Gb': 'STAR_CLUSTER_GLOBULAR',
  'OC': 'STAR_CLUSTER_OPEN',
  'Pn': 'PLANETARY_NEBULA',
  'Nb': 'DIFFUSE_NEBULA',
  'SNR': 'SUPERNOVA_REMNANT',
};

// ─── Search Function ─────────────────────────────────────────────────────────

function searchObjects(params = {}) {
  const {
    name = '',
    type = '',
    limit = 50,
    offset = 0,
    sort_by = 'name',
    sort_descending = false,
    min_magnitude,
    max_magnitude,
    favorites_only,
    visible_only,
    catalogs,
    constellation,
  } = params;

  let results = [...OBJECTS];

  // Filter by name (case-insensitive partial match)
  if (name) {
    const q = name.toLowerCase();
    results = results.filter(o =>
      o.name.toLowerCase().includes(q) ||
      (o.alt_names && o.alt_names.toLowerCase().includes(q))
    );
  }

  // Filter by type
  if (type) {
    const mappedType = TYPE_MAP[type] || type;
    results = results.filter(o => o.type === mappedType || o.type === type);
  }

  // Filter by magnitude range
  if (min_magnitude !== undefined && min_magnitude !== null) {
    results = results.filter(o => o.magnitude >= parseFloat(min_magnitude));
  }
  if (max_magnitude !== undefined && max_magnitude !== null) {
    results = results.filter(o => o.magnitude <= parseFloat(max_magnitude));
  }

  // Filter by catalog name
  if (catalogs && Array.isArray(catalogs) && catalogs.length > 0) {
    results = results.filter(o =>
      catalogs.some(c => o.catalog && o.catalog.toLowerCase() === c.toLowerCase())
    );
  }

  // Filter by constellation
  if (constellation) {
    const c = constellation.toUpperCase();
    results = results.filter(o => o.constellation === c);
  }

  // Sort
  results.sort((a, b) => {
    const aVal = a[sort_by] !== undefined ? a[sort_by] : '';
    const bVal = b[sort_by] !== undefined ? b[sort_by] : '';
    const cmp = typeof aVal === 'string' ? aVal.localeCompare(bVal) : (aVal - bVal);
    return sort_descending ? -cmp : cmp;
  });

  const total = results.length;
  const page = Math.floor(offset / limit) + 1;
  const totalPages = Math.ceil(total / limit) || 1;
  const paged = results.slice(offset, offset + limit);

  // Format for frontend — field names must match protobuf AstronomicalObject schema
  const objects = paged.map((o, i) => ({
    id: `${o.catalog}_${o.name.replace(/\s+/g, '_')}`,
    name: o.name,
    alternate_names: o.alt_names || '',
    object_type: o.type,
    ra_hours: o.ra,
    dec_degrees: o.dec,
    v_magnitude: o.magnitude,
    catalog_name: o.catalog,
    catalog_id: o.catalog_id || '',
    custom_fields: { constellation: o.constellation || '' },
    is_favorite: false,
  }));

  return {
    objects,
    total_count: total,
    total,
    page,
    page_size: limit,
    total_pages: totalPages,
  };
}

module.exports = { searchObjects };
