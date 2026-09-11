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
  // Fail loudly if the method does not exist on the mount client instead of
  // letting @grpc/grpc-js throw an opaque TypeError deep inside the call.
  if (typeof client[method] !== 'function') {
    return Promise.reject(new Error(
      `Method '${method}' does not exist on MountControllerService. ` +
      `The route is wired to the wrong gRPC client/service.`));
  }
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

// ─── Extended-service gRPC clients (weather, power, sequencer, focuser) ──────
// Each service has its own proto file and is reached through a dedicated client
// (weather → 50055, power → 50056, sequencer → 50057, focuser → 50051
// in-process). Before Phase 1 all these routes were (incorrectly) routed
// through the mount client — `grpcCall` — calling methods that do not exist
// there. This factory builds a lazy client plus a call wrapper that validates
// the method exists, so a route wired to the wrong service fails loudly.
const EXT_PROTO_PATH = {
  weather:      path.join(__dirname, '../../../proto/weather.proto'),
  power:        path.join(__dirname, '../../../proto/power.proto'),
  sequencer:    path.join(__dirname, '../../../proto/sequencer.proto'),
  focuser:      path.join(__dirname, '../../../proto/focuser.proto'),
  guider:       path.join(__dirname, '../../../proto/st4_guider.proto'),
  pec:          path.join(__dirname, '../../../proto/pec.proto'),
  camera:       path.join(__dirname, '../../../proto/camera.proto'),
  pulley:       path.join(__dirname, '../../../proto/pulley.proto'),
  pidcal:       path.join(__dirname, '../../../proto/pid_calibration.proto'),
  notifications: path.join(__dirname, '../../../proto/notification.proto'),
};

function makeServiceClient(name, protoPath, serviceName, address) {
  const pkgDef = protoLoader.loadSync(protoPath, {
    keepCase: true,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true,
  });
  const descriptor = grpc.loadPackageDefinition(pkgDef);
  const Svc = descriptor.astro_mount[serviceName];

  let client = null;

  function create() {
    if (client) client.close();
    const creds = config.ssl.enabled
      ? grpc.credentials.createSsl()
      : grpc.credentials.createInsecure();
    client = new Svc(address, creds);
    console.log(`[gRPC] Connected to ${name} service at ${address}`);
    return client;
  }

  function get() {
    if (!client) {
      throw new Error(`${name} gRPC client not initialised. Call create${name}GrpcClient() first.`);
    }
    return client;
  }

  async function call(method, request = {}, timeoutSeconds = 5) {
    // Async so that a missing/uninitialised client rejects cleanly instead of
    // throwing synchronously inside the route handler (which Express 4 would
    // not catch, leaving the request hanging).
    const c = get();
    if (typeof c[method] !== 'function') {
      throw new Error(
        `Method '${method}' does not exist on ${serviceName}. ` +
        `The route is wired to the wrong gRPC client/service.`);
    }
    return new Promise((resolve, reject) => {
      const deadline = new Date();
      deadline.setSeconds(deadline.getSeconds() + timeoutSeconds);
      c[method](request, { deadline }, (error, response) => {
        if (error) reject(error);
        else resolve(response);
      });
    });
  }

  return { create, get, call };
}

const weatherClient = makeServiceClient(
  'Weather', EXT_PROTO_PATH.weather, 'WeatherService',
  `${config.weather.host}:${config.weather.port}`);

const powerClient = makeServiceClient(
  'Power', EXT_PROTO_PATH.power, 'PowerService',
  `${config.power.host}:${config.power.port}`);

const sequencerClient = makeServiceClient(
  'Sequencer', EXT_PROTO_PATH.sequencer, 'SequencerService',
  `${config.sequencer.host}:${config.sequencer.port}`);

const focuserClient = makeServiceClient(
  'Focuser', EXT_PROTO_PATH.focuser, 'FocuserService',
  `${config.focuser.host}:${config.focuser.port}`);

const guiderClient = makeServiceClient(
  'Guider', EXT_PROTO_PATH.guider, 'St4GuiderService',
  `${config.guider.host}:${config.guider.port}`);

const pecClient = makeServiceClient(
  'PEC', EXT_PROTO_PATH.pec, 'PECService',
  `${config.pec.host}:${config.pec.port}`);

const cameraClient = makeServiceClient(
  'Camera', EXT_PROTO_PATH.camera, 'CameraService',
  `${config.camera.host}:${config.camera.port}`);

const pulleyClient = makeServiceClient(
  'Pulley', EXT_PROTO_PATH.pulley, 'PulleyService',
  `${config.pulley.host}:${config.pulley.port}`);

const pidCalibrationClient = makeServiceClient(
  'PidCalibration', EXT_PROTO_PATH.pidcal, 'PidCalibrationService',
  `${config.pidcal.host}:${config.pidcal.port}`);

const notificationsClient = makeServiceClient(
  'Notifications', EXT_PROTO_PATH.notifications, 'NotificationService',
  `${config.notifications.host}:${config.notifications.port}`);

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
  createWeatherGrpcClient: weatherClient.create,
  getWeatherGrpcClient: weatherClient.get,
  weatherGrpcCall: weatherClient.call,
  createPowerGrpcClient: powerClient.create,
  getPowerGrpcClient: powerClient.get,
  powerGrpcCall: powerClient.call,
  createSequencerGrpcClient: sequencerClient.create,
  getSequencerGrpcClient: sequencerClient.get,
  sequencerGrpcCall: sequencerClient.call,
  createFocuserGrpcClient: focuserClient.create,
  getFocuserGrpcClient: focuserClient.get,
  focuserGrpcCall: focuserClient.call,
  createGuiderGrpcClient: guiderClient.create,
  getGuiderGrpcClient: guiderClient.get,
  guiderGrpcCall: guiderClient.call,
  createPecGrpcClient: pecClient.create,
  getPecGrpcClient: pecClient.get,
  pecGrpcCall: pecClient.call,
  createCameraGrpcClient: cameraClient.create,
  getCameraGrpcClient: cameraClient.get,
  cameraGrpcCall: cameraClient.call,
  createPulleyGrpcClient: pulleyClient.create,
  getPulleyGrpcClient: pulleyClient.get,
  pulleyGrpcCall: pulleyClient.call,
  createPidCalibrationGrpcClient: pidCalibrationClient.create,
  getPidCalibrationGrpcClient: pidCalibrationClient.get,
  pidCalibrationGrpcCall: pidCalibrationClient.call,
  createNotificationsGrpcClient: notificationsClient.create,
  getNotificationsGrpcClient: notificationsClient.get,
  notificationsGrpcCall: notificationsClient.call,
};
