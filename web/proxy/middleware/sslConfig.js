/**
 * SSL Configuration Middleware
 *
 * Provides a helper function to get gRPC credentials based on
 * the current SSL configuration.
 */
'use strict';

const grpc = require('@grpc/grpc-js');
const config = require('../config');

/**
 * Returns gRPC credentials based on current SSL configuration.
 * @returns {grpc.ChannelCredentials} SSL or insecure credentials
 */
function getGrpcCredentials() {
  return config.ssl.enabled
    ? grpc.credentials.createSsl()
    : grpc.credentials.createInsecure();
}

module.exports = { getGrpcCredentials };
