#!/usr/bin/env node

/**
 * HTTP/JSON to gRPC Proxy Server for Astronomical Mount Controller
 * This server bridges the web interface to both gRPC services:
 * - Mount Controller Service
 * - Object Database Service
 */

const express = require('express');
const cors = require('cors');
const bodyParser = require('body-parser');
const helmet = require('helmet');
const morgan = require('morgan');
const winston = require('winston');
const path = require('path');
const fs = require('fs');

// gRPC imports
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');

// Configuration
const CONFIG = {
    PORT: process.env.PORT || 8080,
    GRPC_MOUNT_HOST: process.env.GRPC_MOUNT_HOST || 'localhost',
    GRPC_MOUNT_PORT: process.env.GRPC_MOUNT_PORT || 50051,
    GRPC_DB_HOST: process.env.GRPC_DB_HOST || 'localhost',
    GRPC_DB_PORT: process.env.GRPC_DB_PORT || 50052,
    LOG_LEVEL: process.env.LOG_LEVEL || 'info',
    ALLOWED_ORIGINS: process.env.ALLOWED_ORIGINS || '*'
};

// Setup logging
const logger = winston.createLogger({
    level: CONFIG.LOG_LEVEL,
    format: winston.format.combine(
        winston.format.timestamp(),
        winston.format.printf(({ timestamp, level, message }) => {
            return `${timestamp} [${level.toUpperCase()}]: ${message}`;
        })
    ),
    transports: [
        new winston.transports.Console(),
        new winston.transports.File({ filename: 'proxy.log' })
    ]
});

// Load protobuf definitions
const MOUNT_PROTO_PATH = path.join(__dirname, '../proto/mount_controller.proto');
const DB_PROTO_PATH = path.join(__dirname, '../proto/object_database.proto');

if (!fs.existsSync(MOUNT_PROTO_PATH)) {
    logger.error(`Mount proto file not found at: ${MOUNT_PROTO_PATH}`);
    process.exit(1);
}

if (!fs.existsSync(DB_PROTO_PATH)) {
    logger.error(`Database proto file not found at: ${DB_PROTO_PATH}`);
    process.exit(1);
}

// Load mount controller proto
const mountPackageDefinition = protoLoader.loadSync(MOUNT_PROTO_PATH, {
    keepCase: true,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true
});

const mountProtoDescriptor = grpc.loadPackageDefinition(mountPackageDefinition);
const astroMount = mountProtoDescriptor.astro_mount;

// Load object database proto
const dbPackageDefinition = protoLoader.loadSync(DB_PROTO_PATH, {
    keepCase: true,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true
});

const dbProtoDescriptor = grpc.loadPackageDefinition(dbPackageDefinition);
const astroObjects = dbProtoDescriptor.astro_objects;

// Create gRPC clients
const mountGrpcAddress = `${CONFIG.GRPC_MOUNT_HOST}:${CONFIG.GRPC_MOUNT_PORT}`;
const dbGrpcAddress = `${CONFIG.GRPC_DB_HOST}:${CONFIG.GRPC_DB_PORT}`;

const mountGrpcClient = new astroMount.MountControllerService(
    mountGrpcAddress,
    grpc.credentials.createInsecure()
);

const dbGrpcClient = new astroObjects.ObjectDatabaseService(
    dbGrpcAddress,
    grpc.credentials.createInsecure()
);

logger.info(`Mount gRPC client connected to: ${mountGrpcAddress}`);
logger.info(`Database gRPC client connected to: ${dbGrpcAddress}`);

// Helper function to convert protobuf message to plain object
function protobufToObject(message) {
    if (!message) return null;
    
    const obj = {};
    for (const key in message) {
        if (typeof message[key] === 'object' && message[key] !== null) {
            if (message[key].constructor.name === 'Buffer') {
                obj[key] = message[key];
            } else if (Array.isArray(message[key])) {
                obj[key] = message[key].map(item => 
                    typeof item === 'object' ? protobufToObject(item) : item
                );
            } else {
                obj[key] = protobufToObject(message[key]);
            }
        } else {
            obj[key] = message[key];
        }
    }
    return obj;
}

// Helper function to handle gRPC calls with promises
function grpcCall(client, method, request = {}) {
    return new Promise((resolve, reject) => {
        client[method](request, (error, response) => {
            if (error) {
                logger.error(`gRPC call ${method} failed:`, error);
                reject(error);
            } else {
                resolve(response);
            }
        });
    });
}

// Create Express app
const app = express();

// Security middleware
app.use(helmet({
    contentSecurityPolicy: {
        directives: {
            defaultSrc: ["'self'"],
            styleSrc: ["'self'", "'unsafe-inline'", "https://fonts.googleapis.com", "https://cdnjs.cloudflare.com"],
            scriptSrc: ["'self'", "'unsafe-inline'", "'unsafe-eval'", "https://cdnjs.cloudflare.com", "https://cdn.jsdelivr.net"],
            fontSrc: ["'self'", "https://fonts.gstatic.com", "https://cdnjs.cloudflare.com"],
            imgSrc: ["'self'", "data:", "https:"]
        }
    }
}));

// CORS configuration
app.use(cors({
    origin: CONFIG.ALLOWED_ORIGINS === '*' ? '*' : CONFIG.ALLOWED_ORIGINS.split(','),
    methods: ['GET', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
    allowedHeaders: ['Content-Type', 'Authorization']
}));

// Body parsing
app.use(bodyParser.json({ limit: '10mb' }));
app.use(bodyParser.urlencoded({ extended: true, limit: '10mb' }));

// Request logging
app.use(morgan('combined', { stream: { write: message => logger.info(message.trim()) } }));

// Health check endpoint
app.get('/api/health', async (req, res) => {
    try {
        const mountHealth = await grpcCall(mountGrpcClient, 'CheckHealth', { service: 'mount_controller' });
        const mountHealthData = protobufToObject(mountHealth);
        
        // Try to check database health (if available)
        let dbHealthData = { status: 'unknown' };
        try {
            const dbStats = await grpcCall(dbGrpcClient, 'GetDatabaseStats', {});
            dbHealthData = protobufToObject(dbStats);
            dbHealthData.status = 'connected';
        } catch (dbError) {
            dbHealthData = { status: 'disconnected', error: dbError.message };
        }
        
        res.json({
            status: 'healthy',
            services: {
                mount_controller: {
                    status: 'connected',
                    metrics: mountHealthData.metrics || {}
                },
                object_database: dbHealthData
            },
            timestamp: new Date().toISOString()
        });
    } catch (error) {
        logger.error('Health check failed:', error);
        res.status(503).json({
            status: 'unhealthy',
            services: {
                mount_controller: { status: 'disconnected' },
                object_database: { status: 'unknown' }
            },
            timestamp: new Date().toISOString(),
            error: error.message
        });
    }
});

// MOUNT CONTROLLER ENDPOINTS (existing endpoints)

// Get mount state
app.get('/api/state', async (req, res) => {
    try {
        const state = await grpcCall(mountGrpcClient, 'GetState', {});
        res.json(protobufToObject(state));
    } catch (error) {
        logger.error('Failed to get state:', error);
        res.status(500).json({ error: error.message });
    }
});

// Slew to coordinates
app.post('/api/slew', async (req, res) => {
    try {
        const { coordinates } = req.body;
        
        if (!coordinates || coordinates.ra === undefined || coordinates.dec === undefined) {
            return res.status(400).json({ error: 'Missing coordinates (ra, dec required)' });
        }

        const coords = {
            ra: parseFloat(coordinates.ra),
            dec: parseFloat(coordinates.dec),
            objectName: coordinates.objectName || '',
            apply_refraction: coordinates.apply_refraction !== undefined ? coordinates.apply_refraction : true,
            apply_precession: coordinates.apply_precession !== undefined ? coordinates.apply_precession : true,
            apply_nutation: coordinates.apply_nutation !== undefined ? coordinates.apply_nutation : true,
            apply_aberration: coordinates.apply_aberration !== undefined ? coordinates.apply_aberration : true
        };

        await grpcCall(mountGrpcClient, 'SlewToCoordinates', coords);
        
        logger.info(`Slewing to coordinates: RA ${coords.ra}h, Dec ${coords.dec}°`);
        res.json({ 
            success: true, 
            message: 'Slew command sent successfully',
            coordinates: coords 
        });
    } catch (error) {
        logger.error('Slew failed:', error);
        res.status(500).json({ error: error.message });
    }
});

// Track object
app.post('/api/track', async (req, res) => {
    try {
        const { coordinates } = req.body;
        
        if (!coordinates || coordinates.ra === undefined || coordinates.dec === undefined) {
            return res.status(400).json({ error: 'Missing coordinates (ra, dec required)' });
        }

        const coords = {
            ra: parseFloat(coordinates.ra),
            dec: parseFloat(coordinates.dec)
        };

        await grpcCall(mountGrpcClient, 'TrackObject', coords);
        
        logger.info(`Tracking object: RA ${coords.ra}h, Dec ${coords.dec}°`);
        res.json({ 
            success: true, 
            message: 'Tracking started',
            coordinates: coords 
        });
    } catch (error) {
        logger.error('Track failed:', error);
        res.status(500).json({ error: error.message });
    }
});

// Stop mount
app.post('/api/stop', async (req, res) => {
    try {
        await grpcCall(mountGrpcClient, 'Stop', {});
        logger.info('Mount stopped');
        res.json({ success: true, message: 'Mount stopped' });
    } catch (error) {
        logger.error('Stop failed:', error);
        res.status(500).json({ error: error.message });
    }
});

// Park mount
app.post('/api/park', async (req, res) => {
    try {
        await grpcCall(mountGrpcClient, 'Park', {});
        logger.info('Mount parked');
        res.json({ success: true, message: 'Mount parked' });
    } catch (error) {
        logger.error('Park failed:', error);
        res.status(500).json({ error: error.message });
    }
});

// OBJECT DATABASE ENDPOINTS

// Get database statistics
app.get('/api/db/stats', async (req, res) => {
    try {
        const stats = await grpcCall(dbGrpcClient, 'GetDatabaseStats', {});
        res.json(protobufToObject(stats));
    } catch (error) {
        logger.error('Failed to get database stats:', error);
        res.status(500).json({ error: error.message });
    }
});

// Create astronomical object
app.post('/api/db/objects', async (req, res) => {
    try {
        const object = req.body;
        
        if (!object.name) {
            return res.status(400).json({ error: 'Object name is required' });
        }
        
        const response = await grpcCall(dbGrpcClient, 'CreateObject', object);
        const createdObject = protobufToObject(response);
        
        logger.info(`Created object: ${object.name} (ID: ${createdObject.id})`);
        res.status(201).json({
            success: true,
            message: 'Object created successfully',
            object: createdObject
        });
    } catch (error) {
        logger.error('Failed to create object:', error);
        res.status(500).json({ error: error.message });
    }
});

// Get astronomical object by ID
app.get('/api/db/objects/:id', async (req, res) => {
    try {
        const { id } = req.params;
        const object = await grpcCall(dbGrpcClient, 'GetObject', { id });
        res.json(protobufToObject(object));
    } catch (error) {
        logger.error(`Failed to get object ${req.params.id}:`, error);
        res.status(500).json({ error: error.message });
    }
});

// List astronomical objects
app.get('/api/db/objects', async (req, res) => {
    try {
        const { page, pageSize, sortBy, sortDescending, filterType, minMagnitude, maxMagnitude } = req.query;
        
        const request = {
            page: page ? parseInt(page) : 1,
            page_size: pageSize ? parseInt(pageSize) : 50,
            sort_by: sortBy || 'name',
            sort_descending: sortDescending === 'true',
            filter_type: filterType ? parseInt(filterType) : 0,
            min_magnitude: minMagnitude ? parseFloat(minMagnitude) : 0,
            max_magnitude: maxMagnitude ? parseFloat(maxMagnitude) : 0
        };
        
        const objectList = await grpcCall(dbGrpcClient, 'ListObjects', request);
        res.json(protobufToObject(objectList));
    } catch (error) {
        logger.error('Failed to list objects:', error);
        res.status(500).json({ error: error.message });
    }
});

// Search astronomical objects
app.post('/api/db/objects/search', async (req, res) => {
    try {
        const searchRequest = req.body;
        const objectList = await grpcCall(dbGrpcClient, 'SearchObjects', searchRequest);
        res.json(protobufToObject(objectList));
    } catch (error) {
        logger.error('Failed to search objects:', error);
        res.status(500).json({ error: error.message });
    }
});

// Update astronomical object
app.put('/api/db/objects/:id', async (req, res) => {
    try {
        const { id } = req.params;
        const object = { ...req.body, id };
        
        await grpcCall(dbGrpcClient, 'UpdateObject', object);
        
        logger.info(`Updated object: ${id}`);
        res.json({
            success: true,
            message: 'Object updated successfully'
        });
    } catch (error) {
        logger.error(`Failed to update object ${req.params.id}:`, error);
        res.status(500).json({ error: error.message });
    }
});

// Delete astronomical object
app.delete('/api/db/objects/:id', async (req, res) => {
    try {
        const { id } = req.params;
        
        await grpcCall(dbGrpcClient, 'DeleteObject', { id });
        
        logger.info(`Deleted object: ${id}`);
        res.json({
            success: true,
            message: 'Object deleted successfully'
        });
    } catch (error) {
        logger.error(`Failed to delete object ${req.params.id}:`, error);
        res.status(500).json({ error: error.message });
    }
});

// Add object to favorites
app.post('/api/db/favorites/:objectId', async (req, res) => {
    try {
        const { objectId } = req.params;
        const { userId } = req.body;
        
        await grpcCall(dbGrpcClient, 'AddToFavorites', {
            object_id: objectId,
            user_id: userId || 'default'
        });
        
        logger.info(`Added object ${objectId} to favorites for user ${userId || 'default'}`);
        res.json({
            success: true,
            message: 'Object added to favorites'
        });
    } catch (error) {
        logger.error(`Failed to add object ${req.params.objectId} to favorites:`, error);
        res.status(500).json({ error: error.message });
    }
});

// Remove object from favorites
app.delete('/api/db/favorites/:objectId', async (req, res) => {
    try {
        const { objectId } = req.params;
        const { userId } = req.body;
        
        await grpcCall(dbGrpcClient, 'RemoveFromFavorites', {
            object_id: objectId,
            user_id: userId || 'default'
        });
        
        logger.info(`Removed object ${objectId} from favorites for user ${userId || 'default'}`);
        res.json({
            success: true,
            message: 'Object removed from favorites'
        });
    } catch (error) {
        logger.error(`Failed to remove object ${req.params.objectId} from favorites:`, error);
        res.status(500).json({ error: error.message });
    }
});

// Get favorite objects
app.get('/api/db/favorites', async (req, res) => {
    try {
        const objectList = await grpcCall(dbGrpcClient, 'GetFavorites', {});
        res.json(protobufToObject(objectList));
    } catch (error) {
        logger.error('Failed to get favorites:', error);
        res.status(500).json({ error: error.message });
    }
});

// List categories
app.get('/api/db/categories', async (req, res) => {
    try {
        const categoryList = await grpcCall(dbGrpcClient, 'ListCategories', {});
        res.json(protobufToObject(categoryList));
    } catch (error) {
        logger.error('Failed to list categories:', error);
        res.status(500).json({ error: error.message });
    }
});

// Create category
app.post('/api/db/categories', async (req, res) => {
    try {
        const category = req.body;
        
        if (!category.name) {
            return res.status(400).json({ error: 'Category name is required' });
        }
        
        const response = await grpcCall(dbGrpcClient, 'CreateCategory', category);
        const createdCategory = protobufToObject(response);
        
        logger.info(`Created category: ${category.name} (ID: ${createdCategory.id})`);
        res.status(201).json({
            success: true,
            message: 'Category created successfully',
            category: createdCategory
        });
    } catch (error) {
        logger.error('Failed to create category:', error);
        res.status(500).json({ error: error.message });
    }
});

// Assign category to object
app.post('/api/db/objects/:objectId/categories/:categoryId', async (req, res) => {
    try {
        const { objectId, categoryId } = req.params;
        
        await grpcCall(dbGrpcClient, 'AssignCategory', {
            object_id: objectId,
            category_id: categoryId
        });
        
        logger.info(`Assigned category ${categoryId} to object ${objectId}`);
        res.json({
            success: true,
            message: 'Category assigned to object'
        });
    } catch (error) {
        logger.error(`Failed to assign category ${req.params.categoryId} to object ${req.params.objectId}:`, error);
        res.status(500).json({ error: error.message });
    }
});

// Remove category from object
app.delete('/api/db/objects/:objectId/categories/:categoryId', async (req, res) => {
    try {
        const { objectId, categoryId } = req.params;
        
        await grpcCall(dbGrpcClient, 'RemoveCategory', {
            object_id: objectId,
            category_id: categoryId
        });
        
        logger.info(`Removed category ${categoryId} from object ${objectId}`);
        res.json({
            success: true,
            message: 'Category removed from object'
        });
    } catch (error) {
        logger.error(`Failed to remove category ${req.params.categoryId} from object ${req.params.objectId}:`, error);
        res.status(500).json({ error: error.message });
    }
});

// Find visible objects
app.post('/api/db/objects/visible', async (req, res) => {
    try {
        const visibilityRequest = req.body;
        const objectList = await grpcCall(dbGrpcClient, 'FindVisibleObjects', visibilityRequest);
        res.json(protobufToObject(objectList));
    } catch (error) {
        logger.error('Failed to find visible objects:', error);
        res.status(500).json({ error: error.message });
    }
});

// Get tonight's best objects
app.post('/api/db/objects/tonight', async (req, res) => {
    try {
        const tonightRequest = req.body;
        const objectList = await grpcCall(dbGrpcClient, 'GetTonightBestObjects', tonightRequest);
        res.json(protobufToObject(objectList));
    } catch (error) {
        logger.error('Failed to get tonight\'s best objects:', error);
        res.status(500).json({ error: error.message });
    }
});

// Get object visibility
app.post('/api/db/objects/:objectId/visibility', async (req, res) => {
    try {
        const { objectId } = req.params;
        const visibilityRequest = { ...req.body, object_id: objectId };
        
        const visibilityInfo = await grpcCall(dbGrpcClient, 'GetObjectVisibility', visibilityRequest);
        res.json(protobufToObject(visibilityInfo));
    } catch (error) {
        logger.error(`Failed to get visibility for object ${req.params.objectId}:`, error);
        res.status(500).json({ error: error.message });
    }
});

// Serve static files from the web directory
app.use(express.static(path.join(__dirname, '..')));

// Fallback to index.html for SPA routing
app.get('*', (req, res) => {
    res.sendFile(path.join(__dirname, '../index.html'));
});

// Error handling middleware
app.use((err, req, res, next) => {
    logger.error('Unhandled error:', err);
    res.status(500).json({
        error: 'Internal server error',
        message: process.env.NODE_ENV === 'development' ? err.message : undefined
    });
});

// Start server
const server = app.listen(CONFIG.PORT, () => {
    logger.info(`HTTP/JSON proxy server started on port ${CONFIG.PORT}`);
    logger.info(`Mount gRPC server: ${mountGrpcAddress}`);
    logger.info(`Database gRPC server: ${dbGrpcAddress}`);
    logger.info(`Web interface: http://localhost:${CONFIG.PORT}`);
    logger.info(`API endpoint: http://localhost:${CONFIG.PORT}/api/health`);
});

// Graceful shutdown
process.on('SIGTERM', () => {
    logger.info('SIGTERM received, shutting down gracefully');
    server.close(() => {
        logger.info('HTTP server closed');
        process.exit(0);
    });
});

process.on('SIGINT', () => {
    logger.info('SIGINT received, shutting down gracefully');
    server.close(() => {
        logger.info('HTTP server closed');
        process.exit(0);
    });
});

// Export for testing
module.exports = { app, mountGrpcClient, dbGrpcClient, logger };