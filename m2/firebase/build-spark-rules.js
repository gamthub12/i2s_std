const fs = require('node:fs');
const path = require('node:path');
const base = JSON.parse(fs.readFileSync(path.join(__dirname, 'database.rules.json'), 'utf8'));
const rules = base.rules;
const registry = "root.child('registry').child($deviceId)";
const device = `auth != null && ${registry}.child('deviceUid').val() === auth.uid`;
const owner = `auth != null && ${registry}.child('ownerUid').val() === auth.uid`;
const active = `${registry}.child('revoked').val() !== true`;
const deviceEmail = "auth.token.email != null && auth.token.email.matches(/^esp32-[a-f0-9]{12}-[a-f0-9]{16}@device[.]invalid$/) && auth.token.email.beginsWith('esp32-' + $deviceId + '-')";
rules.registry = {
  '$deviceId': {
    '.read': `auth != null && (data.child('deviceUid').val() === auth.uid || data.child('ownerUid').val() === auth.uid || (!data.exists() && (${deviceEmail})))`,
    '.write': `auth != null && ((!newData.exists() && data.child('deviceUid').val() === auth.uid) || (newData.child('deviceUid').val() === auth.uid && ((!data.exists() && (${deviceEmail})) || data.child('deviceUid').val() === auth.uid)))`,
    '.validate': "newData.hasChildren(['deviceUid', 'revoked']) && newData.child('deviceUid').isString() && newData.child('revoked').isBoolean() && (!data.child('ownerUid').exists() || newData.child('ownerUid').val() === data.child('ownerUid').val())",
    deviceUid: { '.validate': 'newData.isString() && newData.val() === auth.uid' },
    ownerUid: { '.validate': "newData.isString() && newData.val().length > 0 && newData.val() !== auth.uid" },
    revoked: { '.validate': 'newData.isBoolean()' },
    '$other': { '.validate': false },
  },
};
const row = rules.devices.$deviceId;
row['.write'] = `(${device}) && !newData.exists()`;
row.inbox['.read'] = `(${active}) && ((${device}) || (${owner}))`;
row.inbox.desired['.write'] = `(${active}) && ((${owner}) || ((!data.exists()) && (${device})))`;
row.inbox.commands.$commandId['.write'] = `(${active}) && (((${owner}) && !data.exists() && newData.exists()) || ((${device}) && data.exists() && !newData.exists()))`;
row.reported['.read'] = `(${active}) && ((${owner}) || (${device}))`;
row.reported['.write'] = `(${active}) && (${device})`;
row.acks['.read'] = `(${active}) && ((${owner}) || (${device}))`;
row.acks.$commandId['.write'] = `(${active}) && (${device})`;
fs.writeFileSync(path.join(__dirname, 'database.spark.rules.json'), JSON.stringify(base, null, 2) + '\n');
console.log('Generated Spark rules: device identity, local ownership assignment, owner commands, device reports.');
