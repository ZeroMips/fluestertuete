// The device settings block, byte for byte as tools/make_config.py builds it (tests/config_vector.json pins both):
// "FLUE", u16 version 1, u16 0, u32 JSON length, u32 CRC-32 of the JSON (all little endian), then UTF-8 JSON.
export const CONFIG_OFFSET = 0x4a0000;
export const CONFIG_SIZE = 0x4000;
export const KEYS = ["lang", "wifi_ssid", "wifi_password", "nas_type", "nas_url", "tls_name", "ca_pem", "root",
  "share_id", "share_password", "dav_user", "dav_password", "ripper_url", "ripper_password", "boombox_url", "dlna_url"];

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

// Only the known keys, in a fixed order, empty ones left out.
export function cleanSettings(settings) {
  const out = {};
  for (const k of KEYS) if (settings[k]) out[k] = String(settings[k]);
  return out;
}

export function packConfig(settings) {
  const body = new TextEncoder().encode(JSON.stringify(cleanSettings(settings)));
  if (body.length + 16 > CONFIG_SIZE) throw new Error(`settings too large: ${body.length} bytes`);
  const out = new Uint8Array(16 + body.length);
  out.set([0x46, 0x4c, 0x55, 0x45]); // "FLUE"
  const v = new DataView(out.buffer);
  v.setUint16(4, 1, true);
  v.setUint16(6, 0, true);
  v.setUint32(8, body.length, true);
  v.setUint32(12, crc32(body), true);
  out.set(body, 16);
  return out;
}
