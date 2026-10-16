// Real board test. Removes only its own temporary owner binding afterwards.
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { randomBytes } = require('node:crypto');
const { spawnSync } = require('node:child_process');
const path = require('node:path');
const config = JSON.parse(readFileSync(path.join(__dirname, '../../android/app/google-services.json'), 'utf8'));
const client = config.client.find(c => c.client_info.android_client_info.package_name === 'vn.local.alarmspeaker');
const key = client.api_key[0].current_key;
const database = config.project_info.firebase_url.replace(/\/$/, '');
const board = process.env.ALARM_BOARD_URL || 'http://192.168.100.169';
const expected = 'b8f862facf84';
const api = 'https://identitytoolkit.googleapis.com/v1/accounts:';
let owner, paired = false, mayBePlaying = false;
async function json(url, method = 'GET', body) {
  const response = await fetch(url, { method, headers: { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(35000) });
  const raw = await response.text();
  return { status: response.status, data: raw ? JSON.parse(raw) : null };
}
function db(p) { return `${database}/${p}.json?auth=${owner.idToken}`; }
async function until(label, read, matches) {
  const end = Date.now() + 35000;
  while (Date.now() < end) {
    const result = await read();
    if (result.status === 200 && matches(result.data)) return result.data;
    await new Promise(resolve => setTimeout(resolve, 1000));
  }
  throw new Error(`Timeout: ${label}`);
}
async function command(type) {
  const started = Date.now();
  const created = await json(db(`devices/${expected}/inbox/commands`), 'POST', {
    type, issuedAt: started, expiresAt: started + 30000,
  });
  assert.equal(created.status, 200);
  const ack = await until(`${type} ACK`, () => json(db(`devices/${expected}/acks/${created.data.name}`)), d => d?.state);
  assert.equal(ack.state, 'done', `${type}: ${ack.state}`);
  console.log(`${type}: ACK done in ${Date.now() - started} ms`);
}
async function main() {
  try {
    const pairing = await json(`${board}/api/pairing`);
    assert.equal(pairing.status, 200);
    assert.equal(pairing.data.deviceId, expected);
    const signup = await json(`${api}signUp?key=${key}`, 'POST', {
      email: `m2-board-${randomBytes(8).toString('hex')}@example.com`,
      password: `Aa1!${randomBytes(16).toString('hex')}`, returnSecureToken: true,
    });
    assert.equal(signup.status, 200, signup.data.error?.message);
    owner = signup.data;
    const result = await json(`${board}/api/pair`, 'POST', {
      deviceId: expected, code: pairing.data.code, idToken: owner.idToken,
    });
    assert.equal(result.status, 200, `Pair HTTP ${result.status}`);
    paired = true;
    const desired = await json(db(`devices/${expected}/inbox/desired`));
    assert.equal(desired.status, 200);
    assert.ok(desired.data?.revision >= 1);
    console.log(`Pair passed; preserved schedule: revision=${desired.data.revision}, alarms=${desired.data.alarmCount}`);
    await until('board online', () => json(db(`devices/${expected}/reported`)), d => d?.sdMounted && Date.now() - d.lastSeen < 30000);
    mayBePlaying = true;
    await command('playNow');
    await until('playing true', () => json(db(`devices/${expected}/reported`)), d => d?.playing === true);
    await new Promise(resolve => setTimeout(resolve, 2000));
    await command('stop');
    await until('playing false', () => json(db(`devices/${expected}/reported`)), d => d?.playing === false);
    mayBePlaying = false;
    console.log('SPARK BOARD PASS: local pairing, real Firebase push commands, play/stop ACK and playback state');
  } finally {
    if (mayBePlaying) {
      const stop = await fetch(`${board}/stop`, { method: 'POST', signal: AbortSignal.timeout(10000) });
      assert.ok(stop.ok, 'Emergency local stop');
    }
    if (paired) {
      const registry = await json(db(`registry/${expected}`));
      assert.equal(registry.data?.ownerUid, owner.localId, 'Cleanup must match temporary owner');
      const cli = path.join(__dirname, '../node_modules/firebase-tools/lib/bin/firebase.js');
      const removed = spawnSync(process.execPath, [cli, 'database:remove', `/registry/${expected}/ownerUid`,
        '--force', '--project', config.project_info.project_id, '--non-interactive'], {
        cwd: path.join(__dirname, '..'), encoding: 'utf8', timeout: 60000,
      });
      assert.equal(removed.status, 0, 'Temporary owner binding removal failed; inspect Firebase CLI');
      console.log('Temporary owner binding removed; board ready for Android pairing');
    }
    if (owner) {
      assert.equal((await json(`${api}delete?key=${key}`, 'POST', { idToken: owner.idToken })).status, 200);
      console.log('Temporary owner Auth account removed');
    }
  }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
