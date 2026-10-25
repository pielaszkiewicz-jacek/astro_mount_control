/**
 * Camera Control Routes
 *
 * Proxies camera operations to the CameraService gRPC service, which is hosted
 * IN-PROCESS inside the mount controller (unified gRPC port 50051) — R3.
 * The service is a clearly-labelled SIMULATED camera ("Simulated Camera",
 * driver "simulated-1.0") — the UI shows it as such via GET /info. No silent
 * simulated fallback here: an unreachable service returns an explicit 503.
 */
'use strict';

const express = require('express');
const router = express.Router();
const grpc = require('@grpc/grpc-js');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFile } = require('child_process');
const { getCameraGrpcClient, cameraGrpcCall } = require('../grpc/client');
const { errorResponse } = require('../grpc/converters');

/**
 * Convert an image buffer into a small base64-encoded PNG thumbnail for the
 * inline preview. Uses ImageMagick (`magick`) when available. Resolves to null
 * when the conversion fails so callers can fall back to a text confirmation.
 */
function generateThumbnail(imageBuffer) {
  return new Promise((resolve) => {
    let tmpDir;
    try {
      tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'astro-cam-'));
      const srcPath = path.join(tmpDir, 'frame.img');
      fs.writeFileSync(srcPath, imageBuffer);

      const magickBin = process.env.IMAGEMAGICK_BIN || 'magick';
      execFile(
        magickBin,
        [srcPath, '-auto-level', '-depth', '8', '-resize', '1024x1024>', 'png:-'],
        { timeout: 30000, maxBuffer: 64 * 1024 * 1024, encoding: 'buffer' },
        (err, stdout) => {
          fs.rmSync(tmpDir, { recursive: true, force: true });
          if (err || !stdout || stdout.length === 0) {
            resolve(null);
          } else {
            resolve(stdout.toString('base64'));
          }
        }
      );
    } catch (err) {
      if (tmpDir) fs.rmSync(tmpDir, { recursive: true, force: true });
      resolve(null);
    }
  });
}

/**
 * GET /api/camera/info
 * Returns camera information and capabilities.
 */
router.get('/info', async (req, res) => {
  try {
    const info = await cameraGrpcCall('GetCameraInfo', {});
    res.json({
      name: info.name || '',
      manufacturer: info.manufacturer || '',
      sensor_name: info.sensor_name || '',
      width: info.width || 0,
      height: info.height || 0,
      pixel_size_um: info.pixel_size_um || 0,
      max_bin: info.max_bin || 1,
      has_filter_wheel: info.has_filter_wheel || false,
      num_filters: info.num_filters || 0,
      filter_names: info.filter_names || [],
      e_ad: info.e_ad || 0,
      read_noise_e: info.read_noise_e || 0,
      full_well_capacity_e: info.full_well_capacity_e || 0,
      bit_depth: info.bit_depth || 0,
      has_cooler: info.has_cooler || false,
      min_cooling_c: info.min_cooling_c || 0,
      connected: info.connected || false,
      driver_version: info.driver_version || '',
    });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * POST /api/camera/expose
 * Start an exposure. StartExposure is a SERVER-STREAMING RPC: the service
 * streams progress and finishes with a "complete" message. The proxy collects
 * the stream and responds once it finishes.
 * Body: { exposure_time_s, gain, offset, binning, filter?: { position } }
 */
router.post('/expose', async (req, res) => {
  const body = req.body || {};
  const request = {
    exposure_time_s: parseFloat(body.exposure_time_s) || 0,
    gain: parseInt(body.gain, 10) || 0,
    offset: parseInt(body.offset, 10) || 0,
    binning: parseInt(body.binning, 10) || 1,
    roi_x: parseInt(body.roi_x, 10) || 0,
    roi_y: parseInt(body.roi_y, 10) || 0,
    roi_width: parseInt(body.roi_width, 10) || 0,
    roi_height: parseInt(body.roi_height, 10) || 0,
    image_type: body.image_type || 'raw',
    save_to_disk: !!body.save_to_disk,
    filename: body.filename || '',
  };
  if (body.filter && body.filter.position !== undefined) {
    request.filter = { position: parseInt(body.filter.position, 10) || 0 };
  }

  const progress = [];
  try {
    // Apply the requested filter first (the camera service holds filter state).
    if (request.filter) {
      await cameraGrpcCall('SetFilter', request.filter);
    }

    const client = getCameraGrpcClient();
    // Total exposure can be long; use a generous deadline (default 10 min).
    const totalMs = Math.max(1000, (request.exposure_time_s || 0) * 1000 + 5000);
    const stream = client.startExposure(request, {
      deadline: new Date(Date.now() + Math.min(totalMs, 3600000)),
    });
    let last = null;
    await new Promise((resolve, reject) => {
      stream.on('data', (msg) => {
        last = msg;
        progress.push({
          elapsed_ms: msg.elapsed_ms,
          remaining_ms: msg.remaining_ms,
          progress_percent: msg.progress_percent,
          status: msg.status || '',
          image_size_bytes: msg.image_size_bytes || 0,
          saved_path: msg.saved_path || '',
        });
      });
      stream.on('end', resolve);
      stream.on('error', reject);
    });
    // Include the final image so clients (web UI) can download it. The camera
    // service streams the image bytes in the final progress message.
    let image = null;
    if (last && last.image && last.image.data && last.image.data.length) {
      const imageBuffer = Buffer.from(last.image.data);
      const format = String(last.image.format || 'fits').toLowerCase();
      // Inline preview is only meaningful for already-displayable formats.
      // FITS stays as a downloadable file with the text confirmation.
      const displayable = format === 'jpeg' || format === 'jpg' || format === 'png';
      const thumbnailBase64 = displayable ? await generateThumbnail(imageBuffer) : null;
      image = {
        format: last.image.format || 'fits',
        size_bytes: imageBuffer.length,
        data_base64: imageBuffer.toString('base64'),
        thumbnail_base64: thumbnailBase64,
        thumbnail_format: thumbnailBase64 ? 'png' : '',
      };
    }

    const finalStatus = last ? last.status : 'failed';
    const message = finalStatus === 'complete' ? 'Exposure complete'
                  : finalStatus === 'aborted' ? 'Exposure aborted'
                  : 'Exposure finished';
    res.json({
      success: finalStatus === 'complete',
      message,
      progress,
      image,
    });
  } catch (err) {
    if (err && err.code === grpc.status.FAILED_PRECONDITION) {
      return errorResponse(
        res, 409, 'Camera not connected',
        'No INDI camera is connected. Select a camera in the Camera Device list and check the USB/power connection.');
    }
    if (err && err.code === grpc.status.UNAVAILABLE) {
      return errorResponse(
        res, 503, 'INDI server unreachable',
        'The controller could not reach the INDI server (indiserver) that drives the camera.');
    }
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * POST /api/camera/abort
 * Abort the current exposure.
 */
router.post('/abort', async (req, res) => {
  try {
    await cameraGrpcCall('AbortExposure', {});
    res.json({ success: true, message: 'Exposure aborted' });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * POST /api/camera/cooler
 * Set cooler target temperature / toggle the cooler.
 * Body: { target_c, enabled? }
 */
router.post('/cooler', async (req, res) => {
  try {
    const body = req.body || {};
    const status = await cameraGrpcCall('SetCooler', {
      target_c: parseFloat(body.target_c) || 0,
      enabled: body.enabled !== undefined ? !!body.enabled : true,
    });
    res.json({
      success: true,
      message: 'Cooler updated',
      enabled: status.enabled || false,
      target_c: status.target_c || 0,
      current_c: status.current_c || 0,
      power_percent: status.power_percent || 0,
      reached_target: status.reached_target || false,
    });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * GET /api/camera/cooler
 * Returns current cooler status.
 */
router.get('/cooler', async (req, res) => {
  try {
    const status = await cameraGrpcCall('GetCoolerStatus', {});
    res.json({
      enabled: status.enabled || false,
      target_c: status.target_c || 0,
      current_c: status.current_c || 0,
      power_percent: status.power_percent || 0,
      reached_target: status.reached_target || false,
      ambient_c: status.ambient_c || 0,
    });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * POST /api/camera/filter
 * Set active filter position.
 * Body: { position: number }
 */
router.post('/filter', async (req, res) => {
  try {
    const position = parseInt((req.body || {}).position, 10) || 0;
    await cameraGrpcCall('SetFilter', { position });
    res.json({ success: true, message: `Filter set to position ${position}` });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * GET /api/camera/filter
 * Returns current filter position.
 */
router.get('/filter', async (req, res) => {
  try {
    const pos = await cameraGrpcCall('GetFilterPosition', {});
    res.json({
      current_position: pos.current_position || 0,
      current_filter: pos.current_filter || '',
      num_filters: pos.num_filters || 0,
      filter_names: pos.filter_names || [],
    });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * GET /api/camera/devices
 * List camera devices available on the INDI server + the active selection.
 */
router.get('/devices', async (req, res) => {
  try {
    const list = await cameraGrpcCall('ListCameras', {});
    res.json({
      devices: (list.devices || []).map((d) => ({ name: d.name || '', label: d.label || d.name || '' })),
      active_device: list.active_device || '',
    });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * POST /api/camera/select
 * Select which INDI camera device to manage.
 * Body: { device: string }  (empty string = simulated camera)
 */
router.post('/select', async (req, res) => {
  try {
    const device = (req.body && req.body.device) || '';
    await cameraGrpcCall('SelectCamera', { device });
    res.json({ success: true, message: device ? `Camera '${device}' selected` : 'Switched to simulated camera' });
  } catch (err) {
    errorResponse(res, 502, 'Failed to select camera', err.message);
  }
});

/**
 * GET /api/camera/properties
 * List all INDI properties exposed by the active camera device.
 */
router.get('/properties', async (req, res) => {
  try {
    const list = await cameraGrpcCall('ListProperties', {});
    res.json({
      properties: (list.properties || []).map((p) => ({
        name: p.name || '',
        label: p.label || p.name || '',
        group: p.group || '',
        type: p.type || '',
        writable: !!p.writable,
        elements: (p.elements || []).map((e) => ({
          name: e.name || '',
          label: e.label || e.name || '',
          number_value: e.number_value || 0,
          number_min: e.number_min || 0,
          number_max: e.number_max || 0,
          number_step: e.number_step || 0,
          switch_state: e.switch_state || 'OFF',
          text_value: e.text_value || '',
        })),
      })),
    });
  } catch (err) {
    errorResponse(res, 503, 'Camera service unavailable', err.message);
  }
});

/**
 * POST /api/camera/property
 * Set a single INDI property element on the active camera device.
 * Body: { property, element, value }
 */
router.post('/property', async (req, res) => {
  try {
    const { property, element, value } = req.body || {};
    if (!property || !element) {
      return errorResponse(res, 400, 'Missing required fields: property, element');
    }
    await cameraGrpcCall('SetProperty', { property, element, value: String(value ?? '') });
    res.json({ success: true, message: `Set ${property}.${element}` });
  } catch (err) {
    errorResponse(res, 502, 'Failed to set camera property', err.message);
  }
});

module.exports = router;
