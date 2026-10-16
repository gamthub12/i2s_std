const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { randomBytes } = require('node:crypto');
const path = require('node:path');
const config = JSON.parse(readFileSync(path.join(__dirname, '../../android/app/google-services.json'), 'utf8'));
const client = config.client.find(c => c.client_info.android_client_info.package_name === 'vn.local.alarmspeaker');
const key = client.api_key[0].current_key;
const database = config.project_info.firebase_url.replace(/\/$/, '');
let token;
async function auth(method, body) {
  const response = await fetch(`https://identitytoolkit.googleapis.com/v1/accounts:${method}?key=${key}`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
    signal: AbortSignal.timeout(20000),
  });
  const result = await response.json();
  if (!response.ok) throw new Error(`Auth ${method}: ${response.status} ${result.error?.message || 'failed'}`);
  return result;
}
async function main() {
  try {
    const email = `m2-smoke-${randomBytes(8).toString('hex')}@example.com`;
    const password = `Aa1!${randomBytes(16).toString('hex')}`;
    const user = await auth('signUp', { email, password, returnSecureToken: true });
    token = user.idToken;
    await auth('signInWithPassword', { email, password, returnSecureToken: true });
    console.log('PASS: cloud Email/Password signup and sign-in');
    const response = await fetch(`${database}/devices.json`, { signal: AbortSignal.timeout(20000) });
    assert.equal(response.status, 401, 'Unauthenticated Database access must be denied');
    console.log('PASS: cloud Realtime Database denies unauthenticated reads');
  } finally {
    if (token) {
      await auth('delete', { idToken: token });
      console.log('Temporary smoke-test account removed');
    }
  }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
