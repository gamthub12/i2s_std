// Uses the already authorized Firebase CLI account; preserves the phone's owner binding.
const assert = require('node:assert/strict');
const { readFileSync, writeFileSync } = require('node:fs');
const path = require('node:path');
const { getGlobalDefaultAccount, getAccessToken } = require('firebase-tools/lib/auth');
const api = require('firebase-tools/lib/api');
require('firebase-tools/lib/logger').logger.level = 'warn';
const config = JSON.parse(readFileSync(path.join(__dirname, '../../android/app/google-services.json'), 'utf8'));
const database = config.project_info.firebase_url.replace(/\/$/, '');
const board = process.env.ALARM_BOARD_URL || 'http://192.168.100.169';
const device = 'b8f862facf84';
let token, original, restoreRevision, mayPlay = false;
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function json(url, method = 'GET', body, admin = false) {
  const headers = { 'Content-Type': 'application/json' };
  if (admin) headers.Authorization = `Bearer ${token}`;
  const response = await fetch(url, { method, headers, body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(20000) });
  assert.equal(response.status, 200, `HTTP ${response.status} on ${admin ? 'Database' : 'board'}`);
  return response.json();
}
const db = (p, method, body) => json(`${database}/${p}.json`, method, body, true);
async function until(label, read, matches) {
  for (let end = Date.now() + 35000; Date.now() < end;) {
    const value = await read();
    if (matches(value)) return value;
    await delay(500);
  }
  throw new Error(`Timeout: ${label}`);
}
async function command(type) {
  const issuedAt = Date.now();
  const { name } = await db(`devices/${device}/inbox/commands`, 'POST', { type, issuedAt, expiresAt: issuedAt + 30000 });
  const ack = await until(`${type} ACK`, () => db(`devices/${device}/acks/${name}`), v => v?.state);
  assert.equal(ack.state, 'done');
  const latency = Date.now() - issuedAt;
  await until(`${type} reported`, () => db(`devices/${device}/reported`), v => v?.playing === (type === 'playNow'));
  assert.equal(await db(`devices/${device}/inbox/commands/${name}`), null, 'ACK queue entry removed');
  console.log(`${type}: ACK ${latency} ms, reported state correct`);
  return latency;
}
async function main() {
  api.setScopes(['https://www.googleapis.com/auth/cloud-platform']);
  const account = getGlobalDefaultAccount();
  assert.ok(account?.tokens?.refresh_token, 'Firebase CLI login required');
  const credentials = await getAccessToken(account.tokens.refresh_token, api.getScopes());
  token = credentials.access_token;
  const registry = await db(`registry/${device}`);
  assert.ok(registry?.deviceUid);
  original = await db(`devices/${device}/inbox/desired`);
  assert.ok(original?.revision);
  writeFileSync(path.join(__dirname, '../../.tools/spark-realtime-schedule-backup.json'), JSON.stringify(original, null, 2));
  const start = await until('SSE connected', () => json(`${board}/health`), h => h.firebase?.streamConnections > 0);
  const latencies = [];
  try {
    // A 20-alarm snapshot exercises fragmented SSE reads and the maximum schedule parser.
    const alarms = {};
    for (let i = 0; i < 20; i++) alarms[`a_${1000 + i}`] = {
      id: 1000 + i, enabled: false, hour: 7, minute: i, days: 127, volume: 80,
      ringSeconds: 30, snoozeMinutes: 5, name: `Realtime test ${i}`, sound: '',
    };
    restoreRevision = original.revision + 2;
    await db(`devices/${device}/inbox/desired`, 'PUT', {
      ...original, revision: original.revision + 1, alarmCount: 20, nextId: 1020, alarms,
    });
    await until('20 alarms applied', () => db(`devices/${device}/reported`), v => v?.appliedRevision === original.revision + 1);
    assert.equal((await json(`${board}/api/alarms`)).alarms.length, 20);
    await db(`devices/${device}/inbox/desired`, 'PUT', { ...original, revision: restoreRevision });
    await until('original schedule restored', () => db(`devices/${device}/reported`), v => v?.appliedRevision === restoreRevision);
    restoreRevision = null;
    console.log('20-alarm realtime snapshot applied; original schedule restored');
    for (let i = 0; i < 3; i++) {
      mayPlay = true;
      latencies.push(await command('playNow'));
      await delay(500);
      latencies.push(await command('stop'));
      mayPlay = false;
    }
    // Let command deletion notifications settle before measuring idle traffic.
    await delay(2500);
    const before = await json(`${board}/health`);
    console.log('Idle measurement started (70 s); no phone interactions during this window');
    await delay(35000);
    console.log('Idle measurement: 35/70 s');
    await delay(35000);
    const after = await json(`${board}/health`);
    const delta = Object.fromEntries(Object.keys(before.firebase).map(k => [k, after.firebase[k] - before.firebase[k]]));
    console.log(`Idle counters: ${JSON.stringify(delta)}`);
    assert.equal(delta.desiredReads, 0, 'No schedule polling when idle');
    assert.equal(delta.commandReads, 0, 'No command polling when idle');
    assert.equal(delta.streamConnections, 0, 'Stream stays connected');
    assert.ok(delta.statusWrites >= 1 && delta.statusWrites <= 2, '60-second heartbeat');
    assert.equal((await db(`registry/${device}`)).ownerUid, registry.ownerUid, 'Phone pairing preserved');
    assert.equal((await json(`${board}/status`)).playing, false);
    const result = { start: start.firebase, latenciesMs: latencies, idleSeconds: 70, idleDelta: delta, health: after };
    writeFileSync(path.join(__dirname, '../../.tools/spark-realtime-result.json'), JSON.stringify(result, null, 2));
    console.log(`REALTIME BOARD PASS; internal free=${after.internalFree}, minimum=${after.internalMinimum}`);
  } finally {
    if (mayPlay) await fetch(`${board}/stop`, { method: 'POST', signal: AbortSignal.timeout(10000) });
    if (restoreRevision) {
      const current = await db(`devices/${device}/inbox/desired`);
      if (current.revision === original.revision + 1)
        await db(`devices/${device}/inbox/desired`, 'PUT', { ...original, revision: restoreRevision });
    }
  }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
