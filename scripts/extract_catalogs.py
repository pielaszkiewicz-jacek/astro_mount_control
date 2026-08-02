#!/usr/bin/env python3
"""Extract catalog subsets from HYG Database and OpenNGC."""
import csv
import os
import sys

CATALOG_DIR = os.path.join(os.path.dirname(__file__), '..', 'web', 'data', 'catalogs')

def extract_hyg_subset(hyg_path, output_path, filter_col, name_col='proper', 
                        ra_col='ra', dec_col='dec', mag_col='mag', spect_col='spect',
                        filter_func=None):
    """Extract subset of HYG database matching a filter condition."""
    count = 0
    with open(hyg_path, 'r', encoding='utf-8') as fin, \
         open(output_path, 'w', newline='', encoding='utf-8') as fout:
        reader = csv.reader(fin)
        header = next(reader)
        
        # Determine column indices
        col_map = {h: i for i, h in enumerate(header)}
        
        # Write output header
        out_header = ['Name', 'RA', 'Dec', 'V-Mag']
        if spect_col and spect_col in col_map:
            out_header.append('Spectrum')
        if 'dist' in col_map:
            out_header.append('Dist')
        writer = csv.writer(fout)
        writer.writerow(out_header)
        
        for row in reader:
            if not row:
                continue
            if filter_func and not filter_func(row, col_map):
                continue
            
            name = row[col_map[name_col]].strip('"') if name_col in col_map else ''
            ra = row[col_map[ra_col]].strip('"') if ra_col in col_map else ''
            dec = row[col_map[dec_col]].strip('"') if dec_col in col_map else ''
            mag = row[col_map[mag_col]].strip('"') if mag_col in col_map else ''
            spect = row[col_map[spect_col]].strip('"') if spect_col in col_map else ''
            dist = row[col_map['dist']].strip('"') if 'dist' in col_map else ''
            
            if not name:
                # Use HIP or HD ID as fallback name
                name = row[col_map['hip']].strip('"') if 'hip' in col_map and row[col_map['hip']] else ''
            if not name:
                continue
            
            out_row = [name, ra, dec, mag]
            if 'Spectrum' in out_header:
                out_row.append(spect)
            if 'Dist' in out_header:
                out_row.append(dist)
            writer.writerow(out_row)
            count += 1
    
    return count

def create_solar_system(output_path):
    """Create solar system bodies CSV."""
    bodies = [
        # name, ra_h, dec_d, v_mag, type
        ('Sun', '0', '0', '-26.74', 'STAR'),
        ('Mercury', '0', '0', '-0.6', 'PLANET'),
        ('Venus', '0', '0', '-4.4', 'PLANET'),
        ('Earth', '0', '0', '-', 'PLANET'),
        ('Mars', '0', '0', '-2.0', 'PLANET'),
        ('Jupiter', '0', '0', '-2.7', 'PLANET'),
        ('Saturn', '0', '0', '-0.2', 'PLANET'),
        ('Uranus', '0', '0', '5.3', 'PLANET'),
        ('Neptune', '0', '0', '7.7', 'PLANET'),
        ('Pluto', '0', '0', '14.0', 'DWARF_PLANET'),
        ('Ceres', '0', '0', '6.9', 'DWARF_PLANET'),
        ('Vesta', '0', '0', '5.3', 'ASTEROID'),
        ('Pallas', '0', '0', '6.5', 'ASTEROID'),
        ('Juno', '0', '0', '7.5', 'ASTEROID'),
        ('Moon', '0', '0', '-12.7', 'MOON'),
        ('Phobos', '0', '0', '9.0', 'MOON'),
        ('Deimos', '0', '0', '10.0', 'MOON'),
        ('Io', '0', '0', '5.0', 'MOON'),
        ('Europa', '0', '0', '5.3', 'MOON'),
        ('Ganymede', '0', '0', '4.6', 'MOON'),
        ('Callisto', '0', '0', '5.0', 'MOON'),
        ('Titan', '0', '0', '8.0', 'MOON'),
        ('Enceladus', '0', '0', '10.0', 'MOON'),
        ('Triton', '0', '0', '10.0', 'MOON'),
        ('Halley', '0', '0', '5.0', 'COMET'),
        ('Hale-Bopp', '0', '0', '-0.5', 'COMET'),
        ('Hyakutake', '0', '0', '3.0', 'COMET'),
        ('ISON', '0', '0', '4.0', 'COMET'),
        ('Churyumov-Gerasimenko', '0', '0', '10.0', 'COMET'),
    ]
    with open(output_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['Name', 'RA', 'Dec', 'V-Mag', 'Type'])
        writer.writerows(bodies)
    return len(bodies)

def main():
    hyg_path = os.path.join(CATALOG_DIR, 'hyg.csv')
    
    if not os.path.exists(hyg_path):
        print(f"ERROR: HYG database not found at {hyg_path}")
        print("Run scripts/download_catalogs.sh first")
        sys.exit(1)
    
    # Extract Bright Stars (have HR number = Yale Bright Star catalog)
    print("==> Extracting Bright Stars (HR catalog from HYG)...")
    count = extract_hyg_subset(
        hyg_path, os.path.join(CATALOG_DIR, 'bright_stars.csv'),
        filter_col='hr',
        filter_func=lambda row, cm: row[cm['hr']].strip('"') != ''
    )
    print(f"    {count} bright stars extracted")
    
    # Extract Hipparcos stars (have HIP number)
    print("==> Extracting Hipparcos stars from HYG...")
    count = extract_hyg_subset(
        hyg_path, os.path.join(CATALOG_DIR, 'hipparcos.csv'),
        filter_col='hip',
        filter_func=lambda row, cm: row[cm['hip']].strip('"') != ''
    )
    print(f"    {count} Hipparcos stars extracted")
    
    # Create solar system bodies
    print("==> Creating solar system bodies...")
    count = create_solar_system(os.path.join(CATALOG_DIR, 'solar_system.csv'))
    print(f"    {count} solar system bodies created")
    
    print()
    print("Done! Remaining presets that need manual data:")
    print("  - sao: SAO Star Catalog (~260k stars) - no public CSV source")
    print("  - double_stars: Washington Double Star Catalog - no public CSV source")

if __name__ == '__main__':
    main()
