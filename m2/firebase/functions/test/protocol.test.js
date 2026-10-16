const test = require("node:test");
const assert = require("node:assert/strict");
const p = require("../protocol");

test("device credentials and pairing code are strict", () => {
  assert.equal(p.validDeviceId("b8f862facf84"), true);
  assert.equal(p.validDeviceId("../../other"), false);
  assert.equal(p.validSecret("a".repeat(64)), true);
  assert.equal(p.validSecret("short"), false);
  assert.equal(p.validPairCode("012345"), true);
  assert.equal(p.validPairCode("12345"), false);
  assert.equal(p.equalDigest(p.digest("a".repeat(64)), "a".repeat(64)), true);
  assert.equal(p.equalDigest(p.digest("a".repeat(64)), "b".repeat(64)), false);
});

test("desired configuration enforces twenty alarms and thirty seconds", () => {
  const alarm = { id: 1, enabled: true, hour: 7, minute: 30, days: 127,
    volume: 50, ringSeconds: 30, snoozeMinutes: 5, name: "Wake", sound: "" };
  const desired = p.emptyDesired();
  assert.equal(p.validDesired(desired), true);
  desired.alarms["1"] = alarm;
  desired.alarmCount = 1;
  assert.equal(p.validDesired(desired), true);
  desired.alarms["1"] = { ...alarm, ringSeconds: 31 };
  assert.equal(p.validDesired(desired), false);
  desired.alarms["1"] = alarm;
  for (let i = 2; i <= 21; i++) desired.alarms[String(i)] = { ...alarm, id: i };
  desired.alarmCount = 21;
  assert.equal(p.validDesired(desired), false);
});
