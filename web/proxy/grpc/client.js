/**
 * Astro Mount Controller - gRPC Client Factory
 *
 * Creates and manages gRPC client connections for both
 * the mount controller and object database services.
 */
'use strict';

const path = require('path');
const grpc = require('@grpc/grpc-js');
const protoLoader = require('@grpc/proto-loader');
const config = require('../config');

// ─── Mount Controller gRPC Client ─────────────────────────────────────────────

const MOUNT_PROTO_PATH = path.join(__dirname, '../../../proto/mount_controller.proto');

const mountPackageDefinition = protoLoader.loadSync(MOUNT_PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const mountProtoDescriptor = grpc.loadPackageDefinition(mountPackageDefinition);
const mountProto = mountProtoDescriptor.astro_mount;

let grpcClient = null;

/**
 * Creates or recreates the gRPC client connection to the mount controller.
 */
function createGrpcClient() {
  const address = `${config.grpc.host}:${config.grpc.port}`;

  if (grpcClient) {
    grpcClient.close();
  }

  const credentials = config.ssl.enabled
    ? grpc.credentials.createSsl()
    : grpc.credentials.createInsecure();

  grpcClient = new mountProto.MountControllerService(address, credentials);

  const sslStatus = config.ssl.enabled ? ' (TLS)' : ' (insecure)';
  console.log(`[gRPC] Connected to mount controller at ${address}${sslStatus}`);
  return grpcClient;
}

/**
 * Returns the current mount controller gRPC client instance.
 */
function getGrpcClient() {
  if (!grpcClient) {
    throw new Error('Mount controller gRPC client not initialized. Call createGrpcClient() first.');
  }
  return grpcClient;
}

/**
 * Wraps a gRPC call into a Promise for async/await usage.
 */
function grpcCall(method, request = {}) {
  const client = getGrpcClient();
  return new Promise((resolve, reject) => {
    const deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + 5); // 5s timeout

    client[method](request, { deadline }, (error, response) => {
      if (error) {
        reject(error);
      } else {
        resolve(response);
      }
    });
  });
}

// ─── Object Database gRPC Client ──────────────────────────────────────────────

const DB_PROTO_PATH = path.join(__dirname, '../../../db/proto/object_database.proto');

const dbPackageDefinition = protoLoader.loadSync(DB_PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const dbProtoDescriptor = grpc.loadPackageDefinition(dbPackageDefinition);
const dbProto = dbProtoDescriptor.astro_objects;

let dbGrpcClient = null;

/**
 * Creates or recreates the gRPC client connection to the object database.
 */
function createDbGrpcClient() {
  const address = `${config.db.host}:${config.db.port}`;

  if (dbGrpcClient) {
    dbGrpcClient.close();
  }

  const credentials = config.ssl.enabled
    ? grpc.credentials.createSsl()
    : grpc.credentials.createInsecure();

  // Allow large messages (e.g. HYG catalog ~14MB CSV data)
  const channelOptions = {
    'grpc.max_receive_message_length': 64 * 1024 * 1024,
    'grpc.max_send_message_length': 64 * 1024 * 1024,
  };

  dbGrpcClient = new dbProto.ObjectDatabaseService(address, credentials, channelOptions);

  console.log(`[gRPC] Connected to object database at ${address}`);
  return dbGrpcClient;
}

/**
 * Returns the current database gRPC client instance.
 */
function getDbGrpcClient() {
  if (!dbGrpcClient) {
    throw new Error('Database gRPC client not initialized. Call createDbGrpcClient() first.');
  }
  return dbGrpcClient;
}

/**
 * Wraps a database gRPC call into a Promise for async/await usage.
 * @param {string} method - The gRPC method name
 * @param {object} request - The request payload
 * @param {number} [timeoutSeconds=10] - Timeout in seconds (default 10, use 300+ for large imports)
 */
function dbGrpcCall(method, request = {}, timeoutSeconds = 10) {
  const client = getDbGrpcClient();
  return new Promise((resolve, reject) => {
    const deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + timeoutSeconds);

    client[method](request, { deadline }, (error, response) => {
      if (error) {
        reject(error);
      } else {
        resolve(response);
      }
    });
  });
}

// ─── Dome gRPC Client ─────────────────────────────────────────────────────────

const DOME_PROTO_PATH = path.join(__dirname, '../../../dome/proto/dome.proto');

const domePackageDefinition = protoLoader.loadSync(DOME_PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const domeProtoDescriptor = grpc.loadPackageDefinition(domePackageDefinition);
const domeProto = domeProtoDescriptor.astro_dome;

let domeGrpcClient = null;

/**
 * Creates or recreates the gRPC client connection to the dome service.
 */
function createDomeGrpcClient() {
  const address = `${config.dome.host}:${config.dome.port}`;

  if (domeGrpcClient) {
    domeGrpcClient.close();
  }

  const credentials = config.ssl.enabled
    ? grpc.credentials.createSsl()
    : grpc.credentials.createInsecure();

  domeGrpcClient = new domeProto.DomeService(address, credentials);

  console.log(`[gRPC] Connected to dome service at ${address}`);
  return domeGrpcClient;
}

/**
 * Returns the current dome gRPC client instance.
 */
function getDomeGrpcClient() {
  if (!domeGrpcClient) {
    throw new Error('Dome gRPC client not initialised. Call createDomeGrpcClient() first.');
  }
  return domeGrpcClient;
}

/**
 * Wraps a dome gRPC call into a Promise for async/await usage.
 */
function domeGrpcCall(method, request = {}, timeoutSeconds = 5) {
  const client = getDomeGrpcClient();
  return new Promise((resolve, reject) => {
    const deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + timeoutSeconds);

    client[method](request, { deadline }, (error, response) => {
      if (error) {
        reject(error);
      } else {
        resolve(response);
      }
    });
  });
}

// ─── Derotator gRPC Client ────────────────────────────────────────────────────

const DEROTATOR_PROTO_PATH = path.join(__dirname, '../../../derotator/proto/derotator.proto');

const derotatorPackageDefinition = protoLoader.loadSync(DEROTATOR_PROTO_PATH, {
  keepCase: true,
  longs: String,
  enums: String,
  defaults: true,
  oneofs: true,
});

const derotatorProtoDescriptor = grpc.loadPackageDefinition(derotatorPackageDefinition);
const derotatorProto = derotatorProtoDescriptor.astro_derotator;

let derotatorGrpcClient = null;

function createDerotatorGrpcClient() {
  const address = `${config.derotator.host}:${config.derotator.port}`;
  if (derotatorGrpcClient) derotatorGrpcClient.close();
  const creds = config.ssl.enabled ? grpc.credentials.createSsl() : grpc.credentials.createInsecure();
  derotatorGrpcClient = new derotatorProto.DerotatorService(address, creds);
  console.log(`[gRPC] Connected to derotator service at ${address}`);
  return derotatorGrpcClient;
}

function getDerotatorGrpcClient() {
  if (!derotatorGrpcClient) {
    throw new Error('Derotator gRPC client not initialised. Call createDerotatorGrpcClient() first.');
  }
  return derotatorGrpcClient;
}

function derotatorGrpcCall(method, request = {}, timeoutSeconds = 5) {
  const client = getDerotatorGrpcClient();
  return new Promise((resolve, reject) => {
    const deadline = new Date();
    deadline.setSeconds(deadline.getSeconds() + timeoutSeconds);
    client[method](request, { deadline }, (error, response) => {
      if (error) reject(error);
      else resolve(response);
    });
  });
}

module.exports = {
  createGrpcClient,
  getGrpcClient,
  grpcCall,
  createDbGrpcClient,
  getDbGrpcClient,
  dbGrpcCall,
  createDomeGrpcClient,
  getDomeGrpcClient,
  domeGrpcCall,
  createDerotatorGrpcClient,
  getDerotatorGrpcClient,
  derotatorGrpcCall,
};
