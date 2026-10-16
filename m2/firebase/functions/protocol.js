const { createHash, timingSafeEqual } = require("node:crypto");

const DEVICE_ID = /^[0-9a-f]{12}$/;
const SECRET = /^[0-9a-f]{64}$/;
const PAIR_CODE = /^[0-9]{6}$/;
const MAX_ALARMS = 20;
const MAX_RING_SECONDS = 30;

function digest(value) {
  return createHash("sha256").update(value, "utf8").digest("hex");
}

function equalDigest(expected, value) {
  if (typeof expected !== "string" || expected.length !== 64) return false;
  const left = Buffer.from(expected, "hex");
  const right = Buffer.from(digest(value), "hex");
  return left.length === right.length && timingSafeEqual(left, right);
}

function validDeviceId(value) { return typeof value === "string" && DEVICE_ID.test(value); }
function validSecret(value) { return typeof value === "string" && SECRET.test(value); }
function validPairCode(value) { return typeof value === "string" && PAIR_CODE.test(value); }

function emptyDesired() {
  return { revision: 1, timezone: "Asia/Ho_Chi_Minh", manualSound: "",
    alarmCount: 0, nextId: 1, alarms: {} };
}

function validAlarm(item) {
  return item && Number.isInteger(item.id) && item.id > 0 &&
    typeof item.enabled === "boolean" && Number.isInteger(item.hour) && item.hour >= 0 && item.hour < 24 &&
    Number.isInteger(item.minute) && item.minute >= 0 && item.minute < 60 &&
    Number.isInteger(item.days) && item.days >= 0 && item.days <= 127 &&
    Number.isInteger(item.volume) && item.volume >= 0 && item.volume <= 100 &&
    Number.isInteger(item.ringSeconds) && item.ringSeconds >= 5 && item.ringSeconds <= MAX_RING_SECONDS &&
    Number.isInteger(item.snoozeMinutes) && item.snoozeMinutes >= 0 && item.snoozeMinutes <= 60 &&
    typeof item.name === "string" && Buffer.byteLength(item.name, "utf8") < 32 &&
    typeof item.sound === "string" && Buffer.byteLength(item.sound, "utf8") < 96 &&
    (item.sound === "" || (/^[^/\\:\x00-\x1f]+\.wav$/i.test(item.sound)));
}

function validDesired(desired) {
  if (!desired || !Number.isInteger(desired.revision) || desired.revision < 1 ||
      desired.timezone !== "Asia/Ho_Chi_Minh" || typeof desired.manualSound !== "string" ||
      !Number.isInteger(desired.alarmCount) || desired.alarmCount < 0 ||
      desired.alarmCount > MAX_ALARMS || !Number.isInteger(desired.nextId) || desired.nextId < 1) return false;
  const alarms = desired.alarms || {};
  const values = Object.values(alarms);
  return values.length === desired.alarmCount && values.every(validAlarm) &&
    (desired.manualSound === "" || /^[^/\\:\x00-\x1f]+\.wav$/i.test(desired.manualSound));
}

module.exports = { digest, equalDigest, validDeviceId, validSecret,
  validPairCode, emptyDesired, validAlarm, validDesired, MAX_ALARMS, MAX_RING_SECONDS };
