const assert = require("node:assert/strict");
const admin = require("../functions/node_modules/firebase-admin");
const deviceId = "b8f862facf84";
admin.initializeApp({ projectId:"demo-alarm",
  databaseURL:"https://demo-alarm-default-rtdb.firebaseio.com" });
const db = admin.database();
const delay = ms => new Promise(resolve => setTimeout(resolve,ms));

async function command(type, key) {
  const now=Date.now();
  await db.ref(`devices/${deviceId}/inbox/commands/${key}`).set({
    type,issuedAt:now,expiresAt:now+30000
  });
  for(let i=0;i<12;i++) {
    await delay(1000);
    const ack=(await db.ref(`devices/${deviceId}/acks/${key}`).get()).val();
    if (ack?.state==="done") return;
  }
  throw new Error(`No ACK for ${type}`);
}

async function main() {
  const desired=(await db.ref(`devices/${deviceId}/inbox/desired`).get()).val();
  const reported=(await db.ref(`devices/${deviceId}/reported`).get()).val();
  assert.ok(desired && reported?.appliedRevision===desired.revision);
  assert.equal(desired.alarmCount,1);
  const marker=Date.now();
  await command("playNow",`t_${marker}_0`);
  console.log("playNow ACK");
  await delay(2000);
  await command("stop",`t_${marker}_1`);
  console.log("stop ACK");
  await delay(3000);
  const final=(await db.ref(`devices/${deviceId}/reported`).get()).val();
  assert.equal(final.playing,false,JSON.stringify(final));
  console.log("BOARD M2 PASS: existing alarm preserved, appliedRevision, playNow/stop ACK, playing=false");
}
main().then(()=>process.exit(0)).catch(e=>{ console.error(e); process.exit(1); });
