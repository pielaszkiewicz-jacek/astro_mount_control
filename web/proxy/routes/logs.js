/**
 * Log Routes — file-based controller log retrieval and SSE streaming.
 *
 * The mount controller writes its logs either through spdlog to
 * `astro-mount.log` or (when the configured directory is not writable, e.g.
 * after `make localuser`) to stdout.  `scripts/astro_env.sh` captures that
 * stdout into `<controller-home>/logs/mount.log`.  These routes read those
 * files directly so the "Controller Logs" panel works in both source-tree and
 * localuser deployments without requiring gRPC log RPCs.
 */
'use strict';

const express = require('express');
const router = express.Router();
const fs = require('fs');
const path = require('path');
const config = require('../config');
const { errorResponse } = require('../grpc/converters');

/**
 * Resolve the directory containing controller log files.
 * Priority: LOG_DIRECTORY env, <controller-home>/logs, config default.
 */
function resolveLogDir() {
  if (process.env.LOG_DIRECTORY) return process.env.LOG_DIRECTORY;
  const localLogs = path.join(__dirname, '../../logs');
  try {
    if (fs.existsSync(localLogs)) return localLogs;
  } catch (_) { /* ignore */ }
  return config.logging.directory;
}

/**
 * Resolve the controller log file, preferring the spdlog file when present
 * and falling back to the captured console log (`mount.log`).
 */
function resolveLogFile(logDir) {
  const candidates = [
    path.join(logDir, 'astro-mount.log'),
    path.join(logDir, 'mount.log'),
  ];
  for (const candidate of candidates) {
    try {
      if (fs.existsSync(candidate) && fs.statSync(candidate).size > 0) {
        return candidate;
      }
    } catch (_) { /* ignore */ }
  }
  // Return the spdlog path so streaming can pick it up once created.
  return candidates[0];
}

function stripAnsi(text) {
  return String(text).replace(/\u001b\[[0-9;]*m/g, '');
}

/**
 * Parse a single spdlog line of the form:
 *   [2026-10-28 23:26:47.123] [info] [main] message
 */
function parseLine(line) {
  const clean = stripAnsi(line).replace(/\r$/, '');
  if (!clean.trim()) return null;
  const match = clean.match(/^\[([^\]]+)\]\s*\[([A-Za-z]+)\](?:\s*\[([^\]]*)\])?\s*(.*)$/);
  if (match) {
    return {
      timestamp: match[1],
      level: match[2].toLowerCase(),
      component: match[3] || '',
      message: match[4],
    };
  }
  return { timestamp: '', level: 'info', component: '', message: clean };
}

function parseLines(lines) {
  const entries = [];
  for (const line of lines) {
    const entry = parseLine(line);
    if (entry) entries.push(entry);
  }
  return entries;
}

/**
 * Read the last `maxLines` lines from `file` without loading the whole file.
 */
function tailLines(file, maxLines) {
  let fd;
  try {
    fd = fs.openSync(file, 'r');
    const stat = fs.fstatSync(fd);
    const size = stat.size;
    if (size === 0) return [];
    const chunkSize = Math.min(size, Math.max(64 * 1024, maxLines * 200));
    const start = Math.max(0, size - chunkSize);
    const buf = Buffer.alloc(chunkSize);
    fs.readSync(fd, buf, 0, chunkSize, start);
    const text = buf.toString('utf8');
    const lines = text.split('\n');
    // When starting mid-file the first line is likely partial.
    if (start > 0) lines.shift();
    return lines.slice(-maxLines);
  } catch (_) {
    return [];
  } finally {
    if (fd !== undefined) fs.closeSync(fd);
  }
}

/**
 * GET /api/logs/stream
 * SSE (Server-Sent Events) endpoint for real-time controller log streaming.
 */
router.get('/stream', (req, res) => {
  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    'Connection': 'keep-alive',
  });

  const logDir = resolveLogDir();
  let file = resolveLogFile(logDir);
  let lastSize = -1;
  let timer = null;

  const sendEvent = (event, data) => {
    res.write(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`);
  };

  const emitInitial = () => {
    sendEvent('init', { logs: parseLines(tailLines(file, 500)) });
    try { lastSize = fs.existsSync(file) ? fs.statSync(file).size : 0; } catch (_) { lastSize = 0; }
  };

  const poll = () => {
    try {
      const current = resolveLogFile(logDir);
      if (current !== file) {
        file = current;
        lastSize = -1;
      }
      if (!fs.existsSync(file)) return;
      const size = fs.statSync(file).size;
      if (lastSize < 0) {
        lastSize = size;
        sendEvent('init', { logs: parseLines(tailLines(file, 500)) });
        return;
      }
      if (size > lastSize) {
        const length = size - lastSize;
        const fd = fs.openSync(file, 'r');
        let text = '';
        try {
          const buf = Buffer.alloc(length);
          fs.readSync(fd, buf, 0, length, lastSize);
          text = buf.toString('utf8');
        } finally {
          fs.closeSync(fd);
        }
        lastSize = size;
        const entries = parseLines(text.split('\n').filter(Boolean));
        if (entries.length > 0) sendEvent('log', entries);
      }
    } catch (_) {
      // Transient read errors are non-fatal; keep polling.
    }
  };

  emitInitial();
  timer = setInterval(poll, 700);

  req.on('close', () => {
    if (timer) clearInterval(timer);
    res.end();
  });
});

/**
 * GET /api/logs
 * Get recent controller log entries.
 * Query params: lines/limit?, level?
 */
router.get('/', (req, res) => {
  try {
    const level = String(req.query.level || '').toLowerCase();
    const limit = parseInt(req.query.lines || req.query.limit, 10) || 500;
    const logDir = resolveLogDir();
    const file = resolveLogFile(logDir);
    let entries = parseLines(tailLines(file, limit));
    if (level && level !== 'all') {
      entries = entries.filter(entry => entry.level === level);
    }
    res.json({ logs: entries, count: entries.length, file: path.basename(file) });
  } catch (err) {
    errorResponse(res, 502, 'Failed to get logs', err.message);
  }
});

module.exports = router;
