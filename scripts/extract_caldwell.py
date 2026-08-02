#!/usr/bin/env python3
"""Extract Caldwell catalog objects from OpenNGC CSV data."""
import csv
import sys
import os

CATALOG_DIR = os.path.join(os.path.dirname(__file__), '..', 'web', 'data', 'catalogs')
CALDWELL_LIST = [
    (1, 188), (2, 40), (3, 4236), (4, 7023), (5, 'IC342'), (6, 6543),
    (7, 2403), (8, 559), (9, 2523), (10, 663), (11, 7635), (12, 6946),
    (13, 457), (14, 869), (15, 6826), (16, 7243), (17, 147), (18, 185),
    (19, 'IC5146'), (20, 7000), (21, 4449), (22, 7662), (23, 891), (24, 1275),
    (25, 2419), (26, 4244), (27, 6888), (28, 752), (29, 5005), (30, 7331),
    (31, 'IC405'), (32, 4631), (33, 6960), (34, 6960), (35, 4889), (36, 4559),
    (37, 6885), (38, 4565), (39, 2392), (40, 3626), (41, 6544), (42, 7006),
    (43, 7814), (44, 7479), (45, 5248), (46, 2261), (47, 6934), (48, 2775),
    (49, 2237), (50, 2244), (51, 'IC1613'), (52, 4697), (53, 3115), (54, 2506),
    (55, 7009), (56, 246), (57, 6822), (58, 2360), (59, 3242), (60, 4038),
    (61, 4039), (62, 247), (63, 7293), (64, 2362), (65, 253), (66, 5694),
    (67, 1097), (68, 6729), (69, 6302), (70, 300), (71, 2477), (72, 55),
    (73, 1851), (74, 3132), (75, 6124), (76, 6231), (77, 5128), (78, 6541),
    (79, 3201), (80, 5139), (81, 6352), (82, 6193), (83, 4945), (84, 5286),
    (85, 'IC2391'), (86, 6397), (87, 1261), (88, 5823), (89, 6087), (90, 2867),
    (91, 2808), (92, 3372), (93, 6752), (94, 4755), (95, 6025), (96, 2516),
    (97, 3766), (98, 4609), (99, 3372), (100, 6744), (101, 'IC2602'), (102, 2070),
    (103, 362), (104, 4833), (105, 104), (106, 6101), (107, 4372), (108, 3195),
]

def format_name(obj_id):
    """Format NGC/IC number with zero-padded 4-digit format used by OpenNGC."""
    if isinstance(obj_id, str) and obj_id.startswith('IC'):
        num = int(obj_id[2:])
        return f"IC{num:04d}"
    else:
        return f"NGC{int(obj_id):04d}"

def main():
    ngc_path = os.path.join(CATALOG_DIR, 'ngc.csv')
    caldwell_path = os.path.join(CATALOG_DIR, 'caldwell.csv')
    
    # Build set of target names
    target_names = set()
    for c_num, obj_id in CALDWELL_LIST:
        target_names.add(format_name(obj_id))
    
    print(f"Looking for {len(target_names)} Caldwell targets...")
    
    # Read NGC CSV and extract matches
    with open(ngc_path, 'r') as f:
        reader = csv.reader(f, delimiter=';')
        header = next(reader)
        
        matches = []
        for row in reader:
            if row and row[0] in target_names:
                matches.append(row)
    
    print(f"Found {len(matches)} matching objects in OpenNGC")
    
    # Write Caldwell CSV
    with open(caldwell_path, 'w', newline='') as f:
        writer = csv.writer(f, delimiter=';')
        writer.writerow(header)
        for row in matches:
            writer.writerow(row)
    
    print(f"Written {len(matches)} objects to {caldwell_path}")
    
    # Show what was found
    for row in matches[:5]:
        print(f"  {row[0]} ({row[4]}) - V={row[9]} - {row[28] if len(row)>28 else ''}")

if __name__ == '__main__':
    main()
