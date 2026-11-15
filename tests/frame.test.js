// Zgodność ramek komponentu sensmos_lora z Hubem: node --test tests/frame.test.js
// Wymaga g++ i libcrypto (OpenSSL) oraz repo sensmos-hub obok (../sensmos-hub) albo HUB_DIR.
const { test } = require('node:test');
const assert = require('node:assert');
const { execFileSync } = require('child_process');
const path = require('path');
const os = require('os');
const fs = require('fs');

const HUB = process.env.HUB_DIR || path.join(__dirname, '..', '..', 'sensmos-hub');
const lora = require(path.join(HUB, 'src', 'lora.js'));
const bin = path.join(os.tmpdir(), 'sensmos_lora_frame_test');
execFileSync('g++', ['-std=c++17', '-I', path.join(__dirname, '..', 'components'), path.join(__dirname, 'frame_test.cpp'), '-o', bin, '-lcrypto']);
const run = (...a) => { const [hex, ms] = execFileSync(bin, a).toString().trim().split(' '); return { hex, ms: +ms }; };

test('ramka AES dekoduje się w Hubie tym samym kluczem', () => {
    const key = lora.keyFromPhrase('moja fraza dzialka');
    const { hex } = run('a1b2c3d4', '3', 'moja fraza dzialka', 't=21.5;h=40;bat=87', '01020304');
    const fr = lora.decode(hex, 'a1b2c3d4', key, false);
    assert.ok(!fr.err, fr.err);
    assert.strictEqual(fr.sub, 3);
    assert.strictEqual(fr.enc, true);
    assert.strictEqual(fr.payload.toString(), 't=21.5;h=40;bat=87');
});

test('ten sam wynik bajt w bajt co encode() Huba (ten sam nonce)', () => {
    const key = lora.keyFromPhrase('fraza-123');
    const { hex } = run('0badcafe', '200', 'fraza-123', 'door=1', 'deadbeef');
    assert.strictEqual(hex, lora.encode('0badcafe', 200, Buffer.from('door=1'), key, Buffer.from('deadbeef', 'hex')));
});

test('zły klucz: Hub odrzuca ramkę (crc_key), ramka jawna wymaga zgody', () => {
    const { hex } = run('a1b2c3d4', '1', 'dobra fraza', 't=1', '00000000');
    assert.strictEqual(lora.decode(hex, 'a1b2c3d4', lora.keyFromPhrase('zla fraza'), false).err, 'crc_key');
    const open = run('a1b2c3d4', '1', '-', 't=1', '00000000').hex;
    assert.strictEqual(lora.decode(open, 'a1b2c3d4', null, false).err, 'open_off');
    assert.strictEqual(lora.decode(open, 'a1b2c3d4', null, true).payload.toString(), 't=1');
});

test('flaga LAST nigdy nie jest ustawiona (serwer takich nie przekazuje)', () => {
    const { hex } = run('a1b2c3d4', '1', 'fraza12345', 't=1', '00000000');
    assert.strictEqual(parseInt(hex.slice(4, 6), 16) & 0x02, 0);
});

test('czas nadawania SF11/125 kHz: typowa ramka ok. 1 s, limit 30 s/h starcza na ok. 30 ramek', () => {
    const { ms } = run('a1b2c3d4', '1', 'fraza12345', 't=21.5;h=40;bat=87', '00000000');
    assert.ok(ms > 700 && ms < 1400, String(ms));
    assert.ok(Math.floor(30000 / ms) >= 20);
});

test('przełącznik: polecenie od noda-bazy (AES + LAST) otwiera się, klucz relay czytany', () => {
    const key = lora.keyFromPhrase('fraza-brama-1');
    const cmd = (t) => { const b = Buffer.from(lora.encode('a1b2c3d4', 5, Buffer.from(t), key, Buffer.from('11223344', 'hex')), 'hex'); b[2] |= 2; return b.toString('hex'); };
    const open = (h, sub = '5', ph = 'fraza-brama-1') => execFileSync(bin, ['open', 'a1b2c3d4', sub, ph, h]).toString().trim();
    assert.strictEqual(open(cmd('c=1731600000;relay=1')), 'c=1731600000;relay=1|1|1');
    assert.strictEqual(open(cmd('c=1731600001;relay=off')), 'c=1731600001;relay=off|off|0');
    assert.strictEqual(open(cmd('c=1731600002;relay=toggle;x=1')), 'c=1731600002;relay=toggle;x=1|toggle|2');
    assert.strictEqual(open(cmd('c=5;relayx=1')), 'c=5;relayx=1|-|-9', 'klucz relayx to nie relay');
    const noLast = lora.encode('a1b2c3d4', 5, Buffer.from('c=9;relay=1'), key, Buffer.from('11223344', 'hex'));
    assert.throws(() => open(noLast), 'bez LAST = nie od bazy (np. ramka innego czujnika)');
    assert.throws(() => open(cmd('c=9;relay=1'), '6'), 'inny sub');
    assert.throws(() => open(cmd('c=9;relay=1'), '5', 'inna-fraza-9'), 'zły klucz');
});
