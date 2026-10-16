const { onRequest, onCall, HttpsError } = require("firebase-functions/v2/https");
const { initializeApp } = require("firebase-admin/app");
const { getAuth } = require("firebase-admin/auth");
const { getDatabase } = require("firebase-admin/database");
const protocol = require("./protocol");

const firebaseConfig = JSON.parse(process.env.FIREBASE_CONFIG || "{}");
const databaseURL = process.env.ALARM_DATABASE_URL || process.env.FIREBASE_DATABASE_URL ||
  (process.env.FIREBASE_DATABASE_EMULATOR_HOST
    ? "https://demo-alarm-default-rtdb.firebaseio.com" : firebaseConfig.databaseURL);
if (!databaseURL) throw new Error("ALARM_DATABASE_URL must be configured before deployment");
initializeApp({ databaseURL });
const db = getDatabase();
const region = "asia-southeast1";

function reply(response, status, body) {
  response.set("Cache-Control", "no-store").status(status).json(body);
}

exports.enrollDevice = onRequest({ region, cors: false }, async (request, response) => {
  if (request.method !== "POST") return reply(response, 405, { error: "method" });
  const { deviceId, secret, code, initialDesired } = request.body || {};
  if (!protocol.validDeviceId(deviceId) || !protocol.validSecret(secret) ||
      !protocol.validPairCode(code) ||
      (initialDesired !== undefined && !protocol.validDesired(initialDesired)))
    return reply(response, 400, { error: "input" });
  const secretHash = protocol.digest(secret);
  const codeHash = protocol.digest(code);
  let outcome = "mismatch";
  const ref = db.ref(`registry/${deviceId}`);
  const tx = await ref.transaction((old) => {
    if (old && !protocol.equalDigest(old.secretHash, secret)) {
      outcome = "mismatch";
      return;
    }
    if (old && old.revoked) { outcome = "revoked"; return; }
    if (old && old.ownerUid) { outcome = "paired"; return old; }
    outcome = "pending";
    if (old && old.codeHash === codeHash && old.failedAttempts >= 5) {
      outcome = "locked";
      return;
    }
    return { secretHash, codeHash, codeExpiresAt: Date.now() + 10 * 60 * 1000,
      failedAttempts: old?.codeHash === codeHash ? (old.failedAttempts || 0) : 0,
      revoked: false };
  }, undefined, false);
  if (!tx.committed) return reply(response, outcome === "revoked" ? 403 : 409, { error: outcome });
  if (initialDesired) {
    await db.ref(`devices/${deviceId}/inbox/desired`).transaction(
      (old) => old || initialDesired, undefined, false);
  }
  return reply(response, 200, { state: outcome });
});

exports.issueDeviceToken = onRequest({ region, cors: false }, async (request, response) => {
  if (request.method !== "POST") return reply(response, 405, { error: "method" });
  const { deviceId, secret } = request.body || {};
  if (!protocol.validDeviceId(deviceId) || !protocol.validSecret(secret))
    return reply(response, 400, { error: "input" });
  const row = (await db.ref(`registry/${deviceId}`).get()).val();
  if (!row || row.revoked || !row.ownerUid || !protocol.equalDigest(row.secretHash, secret))
    return reply(response, 403, { error: "not-paired-or-revoked" });
  const customToken = await getAuth().createCustomToken(`device:${deviceId}`,
    { role: "device", deviceId });
  return reply(response, 200, { customToken });
});

exports.rotateDeviceSecret = onRequest({ region, cors: false }, async (request, response) => {
  if (request.method !== "POST") return reply(response, 405, { error: "method" });
  const { deviceId, oldSecret, newSecret } = request.body || {};
  if (!protocol.validDeviceId(deviceId) || !protocol.validSecret(oldSecret) ||
      !protocol.validSecret(newSecret) || oldSecret === newSecret)
    return reply(response, 400, { error: "input" });
  const ref = db.ref(`registry/${deviceId}`);
  await ref.get();
  const tx = await ref.transaction((old) => {
    if (old === null) return null;
    if (old.revoked || !protocol.equalDigest(old.secretHash, oldSecret)) return;
    return { ...old, secretHash: protocol.digest(newSecret) };
  }, undefined, false);
  return reply(response, tx.committed ? 200 : 403,
    tx.committed ? { state: "rotated" } : { error: "forbidden" });
});

exports.claimDevice = onCall({ region }, async (request) => {
  if (!request.auth) throw new HttpsError("unauthenticated", "Sign in first");
  const { deviceId, code } = request.data || {};
  if (!protocol.validDeviceId(deviceId) || !protocol.validPairCode(code))
    throw new HttpsError("invalid-argument", "Invalid pairing code");
  let outcome = "invalid";
  const ref = db.ref(`registry/${deviceId}`);
  await ref.get();
  const tx = await ref.transaction((old) => {
    if (old === null) { outcome = "missing"; return null; }
    if (old.revoked) { outcome = "invalid"; return; }
    if (old.ownerUid) {
      outcome = old.ownerUid === request.auth.uid ? "owned" : "claimed";
      return;
    }
    if (!old.codeHash || old.codeExpiresAt < Date.now() || old.failedAttempts >= 5) {
      outcome = "expired"; return;
    }
    if (!protocol.equalDigest(old.codeHash, code)) {
      outcome = "wrong";
      return { ...old, failedAttempts: old.failedAttempts + 1 };
    }
    outcome = "claimed";
    return { ...old, ownerUid: request.auth.uid, codeHash: null,
      codeExpiresAt: 0, failedAttempts: 0 };
  }, undefined, false);
  const ownerUid = tx.snapshot.val()?.ownerUid;
  if (ownerUid !== request.auth.uid)
    throw new HttpsError("permission-denied", outcome === "wrong" ? "Wrong code" : "Code unavailable");
  await db.ref().update({
    [`users/${request.auth.uid}/devices/${deviceId}`]: true,
  });
  const desired = db.ref(`devices/${deviceId}/inbox/desired`);
  await desired.transaction((old) => old || protocol.emptyDesired(), undefined, false);
  return { deviceId, state: "paired" };
});

exports.revokeDevice = onCall({ region }, async (request) => {
  if (!request.auth) throw new HttpsError("unauthenticated", "Sign in first");
  const { deviceId } = request.data || {};
  if (!protocol.validDeviceId(deviceId)) throw new HttpsError("invalid-argument", "Invalid device");
  const ref = db.ref(`registry/${deviceId}`);
  await ref.get();
  const tx = await ref.transaction((old) => {
    if (old === null) return null;
    if (old.ownerUid !== request.auth.uid) return;
    return { ...old, revoked: true, codeHash: null, codeExpiresAt: 0 };
  }, undefined, false);
  if (!tx.committed) throw new HttpsError("permission-denied", "Not device owner");
  await db.ref(`users/${request.auth.uid}/devices/${deviceId}`).remove();
  return { deviceId, state: "revoked" };
});
