const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { randomBytes } = require('node:crypto');
const path = require('node:path');
const config = JSON.parse(readFileSync(path.join(__dirname, '../../android/app/google-services.json'), 'utf8'));
const client = config.client.find(c => c.client_info.android_client_info.package_name === 'vn.local.alarmspeaker');
const database = config.project_info.firebase_url.replace(/\/$/, '');
const api = `https://identitytoolkit.googleapis.com/v1/accounts:`;
const key = client.api_key[0].current_key;
const deviceId = randomBytes(6).toString('hex');
const accounts = [];
let device, registryCreated = false;
async function json(url, method = 'GET', body) {
  const response = await fetch(url, { method, headers: { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(20000) });
  return { status: response.status, data: await response.json() };
}
async function account(email) {
  const result = await json(`${api}signUp?key=${key}`, 'POST', {
    email, password: `Aa1!${randomBytes(16).toString('hex')}`, returnSecureToken: true,
  });
  assert.equal(result.status, 200, `Auth signup: ${result.data.error?.message}`);
  accounts.push(result.data);
  return result.data;
}
function db(p, account) {
  return `${database}/${p}.json${account ? `?auth=${account.idToken}` : ''}`;
}
async function main() {
  try {
    device = await account(`esp32-${deviceId}-${randomBytes(8).toString('hex')}@device.invalid`);
    const owner = await account(`m2-owner-${deviceId}@example.com`);
    const stranger = await account(`m2-other-${deviceId}@example.com`);
    const reg = `registry/${deviceId}`;
    const root = `devices/${deviceId}`;
    assert.equal((await json(db(reg, stranger), 'PUT', { deviceUid: stranger.localId, revoked: false })).status, 401);
    assert.equal((await json(db(reg, device))).data, null);
    assert.equal((await json(db(reg, device), 'PUT', { deviceUid: device.localId, revoked: false })).status, 200);
    registryCreated = true;
    assert.equal((await json(db(`${reg}/ownerUid`, owner), 'PUT', owner.localId)).status, 401);
    const desired = { revision: 1, timezone: 'Asia/Ho_Chi_Minh', manualSound: '', alarmCount: 0, nextId: 1 };
    assert.equal((await json(db(`${root}/inbox/desired`, device), 'PUT', desired)).status, 200);
    const lookup = await json(`${api}lookup?key=${key}`, 'POST', { idToken: owner.idToken });
    assert.equal(lookup.data.users[0].localId, owner.localId);
    assert.equal((await json(db(`${reg}/ownerUid`, device), 'PUT', owner.localId)).status, 200);
    assert.equal((await json(db(`${reg}/ownerUid`, device), 'PUT', stranger.localId)).status, 401);
    assert.equal((await json(db(`${root}/inbox`, stranger))).status, 401);
    assert.equal((await json(db(`${root}/inbox/desired`, owner))).data.revision, 1);
    const alarm = { id: 1, enabled: true, hour: 7, minute: 0, days: 127, volume: 80,
      ringSeconds: 30, snoozeMinutes: 5, name: 'Test', sound: '' };
    const next = { ...desired, revision: 2, alarmCount: 1, nextId: 2, alarms: { a_1: alarm } };
    assert.equal((await json(db(`${root}/inbox/desired`, owner), 'PUT', next)).status, 200);
    assert.equal((await json(db(`${root}/inbox/desired`, device), 'PUT', { ...next, revision: 3 })).status, 401);
    assert.equal((await json(db(`${root}/inbox/desired`, owner), 'PUT', {
      ...next, revision: 3, alarms: { a_1: { ...alarm, ringSeconds: 31 } },
    })).status, 401);
    const report = { appliedRevision: 2, lastSeen: Date.now(), clockSynced: true, sdMounted: true, playing: false };
    assert.equal((await json(db(`${root}/reported`, device), 'PUT', report)).status, 200);
    assert.equal((await json(db(`${root}/reported`, owner), 'PUT', report)).status, 401);
    assert.equal((await json(db(`${root}/reported`, owner))).data.appliedRevision, 2);
    const command = { type: 'playNow', issuedAt: Date.now(), expiresAt: Date.now() + 30000 };
    const commandPath = `${root}/inbox/commands/test`;
    assert.equal((await json(db(commandPath, device), 'PUT', command)).status, 401);
    assert.equal((await json(db(commandPath, owner), 'PUT', command)).status, 200);
    assert.equal((await json(db(commandPath, owner), 'PUT', { ...command, type: 'stop' })).status, 401);
    assert.equal((await json(db(`${root}/acks/test`, device), 'PUT', { state: 'done', at: Date.now() })).status, 200);
    assert.equal((await json(db(commandPath, device), 'DELETE')).status, 200);
    assert.equal((await json(db(`${root}/acks/test`, owner))).data.state, 'done');
    console.log('SPARK E2E PASS: device auth, local-owner model, alarm limits, owner commands, device ACK/status, cross-account denial; no Functions');
  } finally {
    if (registryCreated) {
      assert.equal((await json(db(`devices/${deviceId}`, device), 'DELETE')).status, 200, 'Device data cleanup');
      assert.equal((await json(db(`registry/${deviceId}`, device), 'DELETE')).status, 200, 'Registry cleanup');
    }
    for (const user of accounts) {
      const result = await json(`${api}delete?key=${key}`, 'POST', { idToken: user.idToken });
      assert.equal(result.status, 200, 'Temporary Auth account cleanup');
    }
    console.log('Temporary Spark test data/accounts removed');
  }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
