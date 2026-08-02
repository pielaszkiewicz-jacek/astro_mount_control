#!/bin/bash
# Download and extract full astronomical catalog datasets
# Usage: ./scripts/download_catalogs.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CATALOG_DIR="$SCRIPT_DIR/../web/data/catalogs"
mkdir -p "$CATALOG_DIR"

echo "Downloading catalogs to: $CATALOG_DIR"
echo ""

# ─── 1. OpenNGC (NGC, IC, Messier, Caldwell) ─────────────────────────────
echo "==> [1/2] Downloading OpenNGC catalog..."
OPENNGC_URL="https://raw.githubusercontent.com/mattiaverga/OpenNGC/master/database_files"
curl -sL "${OPENNGC_URL}/NGC.csv" -o "${CATALOG_DIR}/ngc.csv"
echo "    NGC:    $(wc -l < "${CATALOG_DIR}/ngc.csv") lines"

# Extract subsets
python3 -c "
import csv, os
d = '${CATALOG_DIR}'
with open(os.path.join(d,'ngc.csv')) as f:
    r = csv.reader(f, delimiter=';')
    h = next(r)
    rows = list(r)

# Messier
with open(os.path.join(d,'messier.csv'),'w',newline='') as f:
    w = csv.writer(f, delimiter=';')
    w.writerow(h)
    for row in rows:
        if len(row)>=24 and row[23]:
            w.writerow(row)
print(f'    Messier: {sum(1 for r in rows if len(r)>=24 and r[23])} objects')

# IC
with open(os.path.join(d,'ic.csv'),'w',newline='') as f:
    w = csv.writer(f, delimiter=';')
    w.writerow(h)
    for row in rows:
        if len(row)>=26 and row[25]:
            w.writerow(row)
print(f'    IC:      {sum(1 for r in rows if len(r)>=26 and r[25])} objects')

# Caldwell
caldwell_ids = [188,40,4236,7023,6543,2403,559,2523,663,7635,6946,457,869,6826,7243,147,185,7000,4449,7662,891,1275,2419,4244,6888,752,5005,7331,4631,6960,4889,4559,6885,4565,2392,3626,6544,7006,7814,7479,5248,2261,6934,2775,2237,2244,4697,3115,2506,7009,246,6822,2360,3242,4038,4039,247,7293,2362,253,5694,1097,6729,6302,300,2477,55,1851,3132,6124,6231,5128,6541,3201,5139,6352,6193,4945,5286,6397,1261,5823,6087,2867,2808,3372,6752,4755,6025,2516,3766,4609,6744,2070,362,4833,104,6101,4372,3195]
caldwell_ic = ['IC342','IC5146','IC405','IC1613','IC2391','IC2602']
targets = {f'NGC{i:04d}' for i in caldwell_ids} | {f'IC{i[2:]:04d}' for i in caldwell_ic}
with open(os.path.join(d,'caldwell.csv'),'w',newline='') as f:
    w = csv.writer(f, delimiter=';')
    w.writerow(h)
    for row in rows:
        if row[0] in targets:
            w.writerow(row)
print(f'    Caldwell: {sum(1 for r in rows if r[0] in targets)} objects')
"

# ─── 2. HYG Database ─────────────────────────────────────────────────────
echo "==> [2/2] Downloading HYG Database v38..."
HYG_URL="https://raw.githubusercontent.com/astronexus/HYG-Database/main/hyg/v3/hyg_v38.csv.gz"
curl -sL "${HYG_URL}" -o /tmp/hyg_v38.csv.gz
gunzip -c /tmp/hyg_v38.csv.gz > "${CATALOG_DIR}/hyg.csv"
rm /tmp/hyg_v38.csv.gz
echo "    HYG:    $(wc -l < "${CATALOG_DIR}/hyg.csv") lines"

# Extract subsets from HYG
python3 -c "
import csv, os
d = '${CATALOG_DIR}'
with open(os.path.join(d,'hyg.csv')) as f:
    r = csv.reader(f)
    h = next(r)
    ci = {name:i for i,name in enumerate(h)}
    rows = list(r)

for name, col, outfile in [('Bright Stars','hr','bright_stars.csv'),('Hipparcos','hip','hipparcos.csv')]:
    out_cols = ['Name','RA','Dec','V-Mag','Spectrum']
    with open(os.path.join(d,outfile),'w',newline='') as f:
        w = csv.writer(f)
        w.writerow(out_cols)
        count = 0
        for row in rows:
            if row[ci[col]].strip('"'):
                name_val = row[ci['proper']].strip('"') or row[ci['hip']].strip('"')
                w.writerow([name_val, row[ci['ra']].strip('"'), row[ci['dec']].strip('"'), row[ci['mag']].strip('"'), row[ci['spect']].strip('"')])
                count += 1
        print(f'    {name}: {count} objects')
"

# Create solar system bodies
python3 -c "
import csv, os
bodies = [
    ('Sun','0','0','-26.74','STAR'),('Mercury','0','0','-0.6','PLANET'),('Venus','0','0','-4.4','PLANET'),
    ('Earth','0','0','-','PLANET'),('Mars','0','0','-2.0','PLANET'),('Jupiter','0','0','-2.7','PLANET'),
    ('Saturn','0','0','-0.2','PLANET'),('Uranus','0','0','5.3','PLANET'),('Neptune','0','0','7.7','PLANET'),
    ('Pluto','0','0','14.0','DWARF_PLANET'),('Ceres','0','0','6.9','DWARF_PLANET'),('Vesta','0','0','5.3','ASTEROID'),
    ('Moon','0','0','-12.7','MOON'),('Io','0','0','5.0','MOON'),('Europa','0','0','5.3','MOON'),
    ('Ganymede','0','0','4.6','MOON'),('Callisto','0','0','5.0','MOON'),('Titan','0','0','8.0','MOON'),
    ('Halley','0','0','5.0','COMET'),('Hale-Bopp','0','0','-0.5','COMET'),
]
with open(os.path.join('${CATALOG_DIR}','solar_system.csv'),'w',newline='') as f:
    w = csv.writer(f)
    w.writerow(['Name','RA','Dec','V-Mag','Type'])
    w.writerows(bodies)
print(f'    Solar System: {len(bodies)} bodies')
"

echo ""
echo "✅ All catalogs ready!"
ls -lhS "${CATALOG_DIR}" | grep -v total
echo ""
echo "NOTE: SAO and Double Stars presets need custom CSV data."
echo "Place files named sao.csv and double_stars.csv in ${CATALOG_DIR}/"
