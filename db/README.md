# Astronomical Object Database Service

An independent gRPC service for managing astronomical objects with SQLite backend.

## Overview

This service provides a comprehensive database management system for astronomical objects, including stars, planets, galaxies, nebulae, and other celestial bodies. It offers a full-featured gRPC API for creating, reading, updating, deleting, and searching objects.

## Features

- **Complete Object Management**: Store and manage detailed astronomical object information
- **Advanced Search**: Search by name, coordinates, magnitude, object type, etc.
- **Favorites System**: User-specific favorite object collections
- **Category Management**: Organize objects into custom categories
- **Visibility Calculations**: Find objects visible from specific locations/times
- **Import/Export**: Support for catalog import/export operations
- **Statistics**: Database usage statistics and metrics

## Database Schema

The service uses SQLite3 with the following main tables:

1. **astronomical_objects**: Core object data with comprehensive astronomical parameters
2. **favorites**: User-object favorite relationships
3. **categories**: Object categories and tags
4. **object_categories**: Object-category assignments
5. **ephemeris_data**: Ephemeris and visibility information

## gRPC API

The service provides a comprehensive gRPC API defined in `proto/object_database.proto`:

### Object Management
- `CreateObject`: Create new astronomical objects
- `GetObject`: Retrieve objects by ID
- `UpdateObject`: Update existing objects
- `DeleteObject`: Remove objects
- `ListObjects`: Paginated object listing with filtering
- `SearchObjects`: Advanced search functionality

### User Features
- `AddToFavorites` / `RemoveFromFavorites`: Manage user favorites
- `GetFavorites`: Retrieve favorite objects
- `CreateCategory` / `ListCategories`: Category management
- `AssignCategory` / `RemoveCategory`: Object categorization

### Observation Planning
- `FindVisibleObjects`: Find objects visible from a location
- `GetTonightBestObjects`: Tonight's best viewing targets
- `GetObjectVisibility`: Visibility information for specific objects

### Database Operations
- `GetDatabaseStats`: Database statistics
- `BackupDatabase` / `RestoreDatabase`: Database maintenance
- `ImportCatalog` / `ExportCatalog`: Catalog operations

## Building and Running

### Prerequisites

- C++17 compiler
- CMake 3.15+
- gRPC and Protobuf
- SQLite3
- uuid library

### Build Instructions

```bash
# From project root
mkdir -p build
cd build
cmake ..
make astro_object_database_server
```

### Running the Server

```bash
# Run with defaults (localhost:50052, astronomy_objects.db)
./build/bin/astro_object_database_server

# Custom configuration
./build/bin/astro_object_database_server --address 0.0.0.0:50052 --db /path/to/database.db

# Show help
./build/bin/astro_object_database_server --help
```

## Web Integration

The service is integrated with the web application through the enhanced proxy server (`web/proxy/server_enhanced.js`). The web interface provides:

1. **Object Management UI**: Create, edit, delete astronomical objects
2. **Object Browser**: Browse and search the database
3. **Favorite Management**: User-specific favorite lists
4. **Observation Planning**: Visibility and observation planning tools
5. **Database Statistics**: Monitor database usage and contents

### API Endpoints

The proxy server exposes RESTful endpoints:

- `GET /api/db/stats`: Database statistics
- `GET /api/db/objects`: List objects with pagination
- `POST /api/db/objects`: Create new object
- `GET /api/db/objects/:id`: Get object by ID
- `PUT /api/db/objects/:id`: Update object
- `DELETE /api/db/objects/:id`: Delete object
- `POST /api/db/objects/search`: Advanced search
- `GET /api/db/favorites`: Get favorite objects
- `POST /api/db/favorites/:objectId`: Add to favorites
- `DELETE /api/db/favorites/:objectId`: Remove from favorites
- `GET /api/db/categories`: List categories
- `POST /api/db/categories`: Create category

## Example Usage

### Using the gRPC API (Python)

```python
import grpc
import object_database_pb2
import object_database_pb2_grpc

channel = grpc.insecure_channel('localhost:50052')
stub = object_database_pb2_grpc.ObjectDatabaseServiceStub(channel)

# Create a new object
object = object_database_pb2.AstronomicalObject(
    name="M31 - Andromeda Galaxy",
    catalog_name="M31",
    object_type=object_database_pb2.ObjectType.GALAXY,
    ra_hours=0.7117,
    dec_degrees=41.2692,
    v_magnitude=3.44
)

response = stub.CreateObject(object)
print(f"Created object with ID: {response.id}")

# Search for objects
search_request = object_database_pb2.ObjectSearchRequest(
    query="Andromeda",
    object_type=object_database_pb2.ObjectType.GALAXY
)

results = stub.SearchObjects(search_request)
print(f"Found {len(results.objects)} objects")
```

### Using the REST API

```bash
# Get database statistics
curl http://localhost:8080/api/db/stats

# List objects (first page, 50 items)
curl http://localhost:8080/api/db/objects?page=1&pageSize=50

# Create a new object
curl -X POST http://localhost:8080/api/db/objects \
  -H "Content-Type: application/json" \
  -d '{
    "name": "M42 - Orion Nebula",
    "catalog_name": "M42",
    "object_type": 5,
    "ra_hours": 5.589,
    "dec_degrees": -5.45,
    "v_magnitude": 4.0
  }'
```

## Database Fields

Astronomical objects support a wide range of fields:

- **Basic Info**: Name, catalog names, alternate names
- **Coordinates**: RA, Dec, proper motion, parallax
- **Photometry**: V, B, J, H, K magnitudes
- **Physical Properties**: Spectral type, mass, radius, temperature
- **Orbital Elements**: For planets, asteroids, etc.
- **Dimensions**: Angular size, apparent dimensions
- **Metadata**: Data source, catalog ID, creation info
- **User Data**: Ratings, notes, favorites, custom fields

## Configuration

The server supports the following command-line options:

- `--address HOST:PORT`: Server address (default: 0.0.0.0:50052)
- `--db PATH`: Database file path (default: astronomy_objects.db)
- `--help`: Show help message

Environment variables for the web proxy:
- `GRPC_DB_HOST`: Database gRPC host (default: localhost)
- `GRPC_DB_PORT`: Database gRPC port (default: 50052)

## Performance Considerations

- The service uses connection pooling for database operations
- Indexes are created on frequently searched fields
- Pagination is supported for large result sets
- Search queries use parameterized statements for security

## Security Notes

- The service uses gRPC with insecure credentials by default (suitable for local networks)
- For production use, enable TLS/SSL encryption
- Input validation is performed on all API endpoints
- SQL injection protection via parameterized queries
- File path validation for database operations

## License

Part of the Astronomical Mount Controller project.