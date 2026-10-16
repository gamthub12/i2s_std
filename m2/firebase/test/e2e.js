const assert = require("node:assert/strict");
const { randomBytes } = require("node:crypto");
const deviceId = randomBytes(6).toString("hex");
const secret = randomBytes(32).toString("hex");
const code = "123456";
const db = "http://127.0.0.1:9000";
const auth = "http://127.0.0.1:9099";
const fn = "http://127.0.0.1:5001/demo-alarm/asia-southeast1";
const ns = "demo-alarm-default-rtdb";
const admin = require("../functions/node_modules/firebase-admin");
admin.initializeApp({ projectId: "demo-alarm", databaseURL: `https://${ns}.firebaseio.com` });

async function json(url, method = "GET", body) {
  const response = await fetch(url, {
    method, headers: { "Content-Type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  let data;
  try { data = await response.json(); } catch { data = null; }
  return { status: response.status, data };
}

function path(p, token) {
  return `${db}/${p}.json?ns=${ns}${token ? `&auth=${token}` : ""}`;
}

async function main() {
  const email = `m2-${Date.now()}@example.test`;
  const signup = await json(`${auth}/identitytoolkit.googleapis.com/v1/accounts:signUp?key=fake-api-key`,
    "POST", { email, password: "test-password-123", returnSecureToken: true });
  assert.equal(signup.status, 200, JSON.stringify(signup));
  const owner = signup.data.idToken;
  assert.ok(owner);

  const enroll = await json(`${fn}/enrollDevice`, "POST", { deviceId, secret, code,
    initialDesired: { revision:1, timezone:"Asia/Ho_Chi_Minh", manualSound:"",
      alarmCount:0, nextId:1, alarms:{} } });
  assert.equal(enroll.status, 200, JSON.stringify(enroll));
  const registry = (await admin.database().ref(`registry/${deviceId}`).get()).val();
  assert.ok(registry?.codeHash);
  const claim = await fetch(`${fn}/claimDevice`, {
    method: "POST", headers: { "Content-Type": "application/json", Authorization: `Bearer ${owner}` },
    body: JSON.stringify({ data: { deviceId, code } }),
  });
  assert.equal(claim.status, 200, await claim.text());

  const issue = await json(`${fn}/issueDeviceToken`, "POST", { deviceId, secret });
  assert.equal(issue.status, 200, JSON.stringify(issue));
  const exchange = await json(`${auth}/identitytoolkit.googleapis.com/v1/accounts:signInWithCustomToken?key=fake-api-key`,
    "POST", { token: issue.data.customToken, returnSecureToken: true });
  assert.equal(exchange.status, 200, JSON.stringify(exchange));
  const device = exchange.data.idToken;
  assert.ok(device);

  const desiredPath = `devices/${deviceId}/inbox/desired`;
  const initial = await json(path(desiredPath, owner));
  assert.equal(initial.status, 200);
  assert.equal(initial.data.alarmCount, 0);
  const desired = { revision: 2, timezone: "Asia/Ho_Chi_Minh", manualSound: "",
    alarmCount: 1, nextId: 2, alarms: { "a_1": {
      id: 1, enabled: true, hour: 7, minute: 30, days: 127, volume: 80,
      ringSeconds: 30, snoozeMinutes: 5, name: "Wake", sound: "",
    } } };
  assert.equal((await json(path(desiredPath, owner), "PUT", desired)).status, 200);
  assert.equal((await json(path(desiredPath, device))).data.revision, 2);
  assert.equal((await json(path(desiredPath, device), "PUT", desired)).status, 401);
  assert.equal((await json(path(desiredPath), "GET")).status, 401);

  const invalid = { ...desired, revision: 3, alarms: { "a_1": {
    ...desired.alarms["a_1"], ringSeconds: 31 } } };
  assert.equal((await json(path(desiredPath, owner), "PUT", invalid)).status, 401);
  const report = { appliedRevision: 2, lastSeen: Date.now(), clockSynced: true,
    sdMounted: true, playing: false };
  assert.equal((await json(path(`devices/${deviceId}/reported`, device), "PUT", report)).status, 200);
  assert.equal((await json(path(`devices/${deviceId}/reported`, owner))).data.appliedRevision, 2);
  assert.equal((await json(path(`devices/${deviceId}/reported`, owner), "PUT", report)).status, 401);
  const command = { type: "playNow", issuedAt: Date.now(), expiresAt: Date.now() + 30000 };
  assert.equal((await json(path(`devices/${deviceId}/inbox/commands/test001`, owner),
    "PUT", command)).status, 200);
  assert.equal((await json(path(`devices/${deviceId}/inbox/commands`, device))).data.test001.type,
    "playNow");
  const nextSecret = randomBytes(32).toString("hex");
  assert.equal((await json(`${fn}/rotateDeviceSecret`, "POST",
    { deviceId, oldSecret: secret, newSecret: nextSecret })).status, 200);
  assert.equal((await json(`${fn}/issueDeviceToken`, "POST", { deviceId, secret })).status, 403);
  assert.equal((await json(`${fn}/issueDeviceToken`, "POST",
    { deviceId, secret: nextSecret })).status, 200);
  const revoke = await fetch(`${fn}/revokeDevice`, {
    method: "POST", headers: { "Content-Type": "application/json", Authorization: `Bearer ${owner}` },
    body: JSON.stringify({ data: { deviceId } }),
  });
  assert.equal(revoke.status, 200, await revoke.text());
  assert.equal((await json(`${fn}/issueDeviceToken`, "POST",
    { deviceId, secret: nextSecret })).status, 403);
  assert.equal((await json(path(desiredPath, device))).status, 401);
  console.log("M2 emulator E2E PASS: pairing, tokens, desired, permissions, report, command, rotate, revoke");
}
main().then(() => process.exit(0)).catch(error => { console.error(error); process.exit(1); });
