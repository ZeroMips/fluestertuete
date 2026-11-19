// Setup page: form -> settings block -> flash over Web Serial (esptool-js), then read the device's log and
// translate it into plain status lines. No server: nothing here leaves the browser except over USB.
import { ESPLoader, Transport } from "./vendor/esptool-js-0.7.0.mjs";
import { CONFIG_OFFSET, KEYS, packConfig } from "./config.js";
import { TEXTS, format } from "./i18n.js";
import { makeZip } from "./zip.js";

const $ = (id) => document.getElementById(id);
const FLASH_BAUD = 921600;
const LOG_BAUD = 115200;
const WATCH_MS = 120000;

let lang = "de";
let boards = [];
let busy = false;

// ---------------------------------------------------------------- language

function t(key, vars) {
  return format((TEXTS[lang] && TEXTS[lang][key]) || TEXTS.de[key] || key, vars);
}

function setLang(l) {
  lang = TEXTS[l] ? l : "de";
  try { localStorage.setItem("lang", lang); } catch { /* private mode: fine */ }
  document.documentElement.lang = lang;
  document.title = t("title");
  for (const el of document.querySelectorAll("[data-t]")) el.textContent = t(el.dataset.t);
  for (const b of document.querySelectorAll("[data-lang]")) b.setAttribute("aria-pressed", String(b.dataset.lang === lang));
  showNasType();
}

function initialLang() {
  try { const saved = localStorage.getItem("lang"); if (saved) return saved; } catch { /* ignore */ }
  return (navigator.language || "de").toLowerCase().startsWith("de") ? "de" : "en";
}

// ---------------------------------------------------------------- form

const nasType = () => document.querySelector("input[name=nas_type]:checked").value;
const board = () => document.querySelector("input[name=board]:checked")?.value;

function showNasType() {
  const type = nasType();
  for (const el of document.querySelectorAll("[data-nas]")) el.hidden = el.dataset.nas !== type;
  $("root-hint").textContent = t(type === "synology" ? "root_hint_synology" : "root_hint_webdav");
}

// "https://192.168.1.10:5001/sharing/AbCdEf" -> {id: "AbCdEf", origin: "https://192.168.1.10:5001"}
function parseShareLink(text) {
  try {
    const u = new URL(text.trim());
    const id = u.pathname.split("/").filter(Boolean).pop() || "";
    return { id, origin: u.hostname.endsWith("gofile.me") ? "" : u.origin };
  } catch {
    return { id: text.trim(), origin: "" };
  }
}

function boomboxDefault(ripperUrl) {
  try {
    const u = new URL(ripperUrl);
    return `${u.protocol}//${u.hostname}:8081`;
  } catch {
    return "";
  }
}

function collect() {
  const v = (id) => $(id).value.trim();
  const type = nasType();
  const s = {
    lang: $("lang").value,
    wifi_ssid: v("wifi_ssid"),
    wifi_password: $("wifi_password").value,
    nas_type: type,
    root: v("root"),
    tls_name: v("tls_name"),
    ca_pem: $("ca_pem").value.trim(),
    dlna_url: v("dlna_url"),
    ripper_url: v("ripper_url").replace(/\/+$/, ""),
    ripper_password: $("ripper_password").value,
    boombox_url: v("boombox_url").replace(/\/+$/, ""),
  };
  if (s.root && !s.root.startsWith("/")) s.root = "/" + s.root;
  if (type === "synology") {
    const link = parseShareLink(v("share_link"));
    s.share_id = link.id;
    s.share_password = $("share_password").value;
    s.nas_url = v("nas_url_synology") || link.origin;
  } else {
    s.nas_url = v("nas_url_webdav");
    s.dav_user = v("dav_user");
    s.dav_password = $("dav_password").value;
  }
  s.nas_url = (s.nas_url || "").replace(/\/+$/, "");
  if (s.ripper_url && !s.boombox_url) s.boombox_url = boomboxDefault(s.ripper_url);
  return s;
}

// Returns the labels of missing required fields, marking them.
function validate(s) {
  const need = [["wifi_ssid", s.wifi_ssid], ["root", s.root]];
  if (s.nas_type === "synology") need.push(["share_link", s.share_id], ["nas_url_synology", s.nas_url]);
  else need.push(["nas_url_webdav", s.nas_url]);
  const labels = { share_link: "share_link", nas_url_synology: "nas_url", nas_url_webdav: "dav_url" };
  const missing = [];
  for (const [id, value] of need) {
    $(id).classList.toggle("invalid", !value);
    if (!value) missing.push(t(labels[id] || id));
  }
  if (!board()) missing.push(t("board"));
  return missing;
}

// ---------------------------------------------------------------- save / load

function saveSettings() {
  const s = collect();
  const file = { fluestertuete: 1, board: board(), share_link: $("share_link").value.trim(), settings: s };
  const blob = new Blob([JSON.stringify(file, null, 1)], { type: "application/json" });
  const a = Object.assign(document.createElement("a"), { href: URL.createObjectURL(blob), download: "fluestertuete-settings.json" });
  a.click();
  URL.revokeObjectURL(a.href);
}

function loadSettings(file) {
  file.text().then((text) => {
    const data = JSON.parse(text);
    const s = data.settings || data; // also accepts a plain settings file as tools/make_config.py reads it
    for (const k of KEYS) if ($(k) && typeof s[k] === "string") $(k).value = s[k];
    if (s.nas_type) document.querySelector(`input[name=nas_type][value=${s.nas_type === "webdav" ? "webdav" : "synology"}]`).checked = true;
    if (s.nas_type === "webdav") $("nas_url_webdav").value = s.nas_url || "";
    else $("nas_url_synology").value = s.nas_url || "";
    $("share_link").value = data.share_link || s.share_id || "";
    if (data.board) { const r = document.querySelector(`input[name=board][value=${data.board}]`); if (r) r.checked = true; }
    showNasType();
  }).catch((e) => status("bad", t("error", { msg: e.message })));
}

// ---------------------------------------------------------------- package for esptool (no Web Serial)

const hex = (n) => "0x" + n.toString(16);

async function downloadPackage() {
  const s = collect();
  const missing = validate(s);
  $("status").textContent = "";
  if (missing.length) {
    status("bad", t("missing") + missing.join(", "));
    return;
  }
  const manifest = boards.find((b) => b.board === board());
  if (!manifest) {
    status("bad", t("no_firmware"));
    return;
  }
  try {
    const parts = await fetchFirmware(manifest);
    // The app image gets a plain name; bootloader and partition table keep theirs.
    const named = parts.map((p) => ({ ...p, name: p.address === 0x20000 ? "firmware.bin" : p.name }));
    const base = `python -m esptool --chip ${manifest.chip} --port PORT --baud 460800`;
    const write = (list) => `${base} write_flash ` + list.map((p) => `${hex(p.address)} ${p.name}`).join(" ");
    const settings = { name: "settings.bin", address: CONFIG_OFFSET };
    const guide = t("guide", {
      board: manifest.name,
      version: manifest.version,
      tdeck_hint: manifest.board === "tdeck" ? t("guide_tdeck") : "",
      install: write([...named, settings]),
      erase: `${base} erase_flash`,
      settings: write([settings]),
    });
    const zip = makeZip([
      ...named.map((p) => ({ name: p.name, data: p.data })),
      { name: "settings.bin", data: packConfig(s) },
      { name: lang === "de" ? "ANLEITUNG.txt" : "README.txt", data: guide.replace(/\n/g, "\r\n") }, // readable in Notepad
    ]);
    const a = Object.assign(document.createElement("a"), {
      href: URL.createObjectURL(zip),
      download: `fluestertuete-${manifest.board}.zip`,
    });
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  } catch (e) {
    status("bad", t("error", { msg: e.message || String(e) }));
  }
}

// ---------------------------------------------------------------- status output

function status(kind, text) {
  const li = document.createElement("li");
  li.className = kind;
  li.textContent = text;
  $("status").append(li);
  li.scrollIntoView({ block: "nearest" });
}

function rawLog(line) {
  $("raw-log-box").hidden = false;
  const pre = $("raw-log");
  pre.textContent += line + "\n";
  if (pre.textContent.length > 60000) pre.textContent = pre.textContent.slice(-50000);
  pre.scrollTop = pre.scrollHeight;
}

function progress(pct) {
  const p = $("progress");
  p.hidden = pct === null;
  if (pct !== null) p.value = pct;
}

// ---------------------------------------------------------------- flashing

const chipKey = (name) => String(name).toLowerCase().replace(/[^a-z0-9]/g, "");

async function fetchFirmware(manifest) {
  return Promise.all(manifest.parts.map(async (p) => {
    const r = await fetch(`firmware/${manifest.board}/${p.file}`);
    if (!r.ok) throw new Error(`${p.file}: HTTP ${r.status}`);
    return { data: new Uint8Array(await r.arrayBuffer()), address: p.offset, name: p.file };
  }));
}

async function run(full) {
  if (busy) return;
  const s = collect();
  const missing = validate(s);
  $("status").textContent = "";
  if (missing.length) {
    status("bad", t("missing") + missing.join(", "));
    return;
  }
  let manifest = boards.find((b) => b.board === board());
  if (!manifest) {
    status("bad", t("no_firmware"));
    return;
  }

  let port;
  try {
    port = await navigator.serial.requestPort({});
  } catch {
    status("bad", t("cancelled"));
    return;
  }
  busy = true;
  setButtons();
  $("raw-log").textContent = "";
  let transport;
  try {
    status("info", t("connecting"));
    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport,
      baudrate: FLASH_BAUD,
      romBaudrate: LOG_BAUD,
      terminal: { clean() {}, writeLine: rawLog, write: rawLog },
    });
    await loader.main();
    // The chip tells the boards apart (ESP32: Core2, ESP32-S3: T-Deck): follow it rather than refuse.
    const found = loader.chip.CHIP_NAME;
    if (chipKey(found) !== chipKey(manifest.chip)) {
      const match = boards.filter((b) => chipKey(b.chip) === chipKey(found));
      if (match.length !== 1) throw new Error(t("wrong_chip", { found, want: manifest.chip.toUpperCase() }));
      manifest = match[0];
      document.querySelector(`input[name=board][value=${manifest.board}]`).checked = true;
      status("info", t("board_switched", { name: manifest.name }));
    }
    const files = full ? await fetchFirmware(manifest) : [];
    files.push({ data: packConfig(s), address: CONFIG_OFFSET, name: "settings" });
    if (full && $("erase_all").checked) status("info", t("erasing"));
    status("info", t("writing", { file: files[0].name, pct: 0 }));
    const line = $("status").lastChild;
    await loader.writeFlash({
      fileArray: files.map(({ data, address }) => ({ data, address })),
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: full && $("erase_all").checked,
      compress: true,
      reportProgress: (i, written, total) => {
        const pct = Math.round((written / total) * 100);
        progress(((i + written / total) / files.length) * 100);
        line.textContent = t("writing", { file: files[i].name, pct });
      },
    });
    await loader.after("hard_reset");
    await transport.disconnect();
    transport = null;
    progress(null);
    status("ok", t("done_flash"));
    await watch(port);
  } catch (e) {
    status("bad", t("error", { msg: e.message || String(e) }));
  } finally {
    if (transport) await transport.disconnect().catch(() => {});
    progress(null);
    busy = false;
    setButtons();
  }
}

// ---------------------------------------------------------------- device log after the restart

// Device log lines (ESP-IDF format "I (1234) tag: text") mapped to status lines. Returns true when done.
function interpret(line, seen) {
  const once = (key, kind, text) => {
    if (seen.has(key)) return;
    seen.add(key);
    status(kind, text);
  };
  let m;
  if (/config: (Synology|WebDAV) at/.test(line)) once("settings", "ok", t("log_settings_ok"));
  else if (/config: not usable/.test(line)) { once("settings", "bad", t("log_settings_bad")); return true; }
  else if ((m = line.match(/net: got ([\d.]+)/))) once("wifi", "ok", t("log_wifi_ok", { ip: m[1] }));
  else if ((m = line.match(/net: disconnected, reason (\d+)/))) {
    const r = Number(m[1]);
    // 201 no AP found; 2, 15, 202, 204 auth failed / 4-way handshake timeout (wrong password)
    if (r === 201) once("wifi-bad", "bad", t("log_wifi_notfound"));
    else if ([2, 15, 202, 204, 205, 210].includes(r)) once("wifi-bad", "bad", t("log_wifi_password"));
    else once(`wifi-${r}`, "info", t("log_wifi_other", { reason: r }));
  } else if (/nas: logged in|dav: folder reachable/.test(line)) once("nas", "ok", t("log_nas_ok"));
  else if (/nas: login refused|HTTP 401|HTTP 403/.test(line)) once("nas-bad", "bad", t("log_nas_refused"));
  else if (/dav: PROPFIND .*HTTP 404|list failed, code 408/.test(line)) once("nas-404", "bad", t("log_nas_notfound"));
  else if (/certificate|x509|mbedtls_ssl_handshake returned -0x2700/i.test(line)) once("cert", "bad", t("log_cert"));
  else if ((m = line.match(/index: (\d+) entries/))) once("library", "ok", t("log_library", { n: m[1] }));
  // Done once the NAS answered and the library is there (from flash at boot, or after the first crawl).
  return seen.has("nas") && (seen.has("library") || /ui: \/.*folders/.test(line));
}

async function reopen(port) {
  // A chip with built-in USB (T-Deck) leaves the bus while it restarts: find the port again.
  const info = port.getInfo();
  for (let i = 0; i < 20; i++) {
    const ports = await navigator.serial.getPorts();
    const same = ports.find((p) => p === port) ||
      ports.find((p) => p.getInfo().usbVendorId === info.usbVendorId && p.getInfo().usbProductId === info.usbProductId);
    if (same) {
      try {
        await same.open({ baudRate: LOG_BAUD });
        await same.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
        return same;
      } catch { /* not back yet */ }
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  return null;
}

async function watch(port) {
  status("info", t("watching"));
  const p = await reopen(port);
  if (!p) {
    status("info", t("log_lost"));
    return;
  }
  const reader = p.readable.getReader();
  const decoder = new TextDecoder();
  const seen = new Set();
  const stop = setTimeout(() => reader.cancel().catch(() => {}), WATCH_MS);
  let buf = "";
  let done = false;
  try {
    while (!done) {
      const { value, done: ended } = await reader.read();
      if (ended) break;
      buf += decoder.decode(value, { stream: true });
      let nl;
      while ((nl = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, nl).replace(/\x1b\[[0-9;]*m/g, "").trimEnd(); // without ANSI colours
        buf = buf.slice(nl + 1);
        if (!line) continue;
        rawLog(line);
        if (interpret(line, seen)) done = true;
      }
    }
  } catch { /* port gone */ }
  clearTimeout(stop);
  reader.releaseLock();
  await p.close().catch(() => {});
  if (!done) status("info", t("log_lost"));
}

// ---------------------------------------------------------------- start

function setButtons() {
  const ok = "serial" in navigator && boards.length > 0;
  for (const id of ["install", "settings-only"]) $(id).disabled = busy || !ok;
  $("package").disabled = busy || boards.length === 0;
}

async function loadBoards() {
  try {
    const r = await fetch("firmware/index.json", { cache: "no-store" });
    boards = r.ok ? await r.json() : [];
    // Each manifest has the files; the index only names the boards.
    boards = await Promise.all(boards.map(async (b) => (await fetch(`firmware/${b.board}/manifest.json`, { cache: "no-store" })).json()));
  } catch {
    boards = [];
  }
  const box = $("boards");
  for (const b of boards) {
    const label = document.createElement("label");
    label.className = "choice";
    label.innerHTML = `<input type="radio" name="board" value="${b.board}"> <span></span>`;
    label.querySelector("span").textContent = b.name;
    box.append(label);
  }
  const first = box.querySelector("input");
  if (first) first.checked = true;
  $("fw-version").textContent = boards.map((b) => `${b.name} ${b.version}`).join(", ") || "–";
  setButtons();
}

document.addEventListener("DOMContentLoaded", async () => {
  if (!("serial" in navigator)) {
    $("browser-missing").hidden = false;
    $("offline-box").open = true;
  }
  for (const b of document.querySelectorAll("[data-lang]")) b.addEventListener("click", () => setLang(b.dataset.lang));
  for (const r of document.querySelectorAll("input[name=nas_type]")) r.addEventListener("change", showNasType);
  $("share_link").addEventListener("change", () => {
    const { origin } = parseShareLink($("share_link").value);
    if (origin && !$("nas_url_synology").value) $("nas_url_synology").value = origin;
  });
  $("install").addEventListener("click", () => run(true));
  $("settings-only").addEventListener("click", () => run(false));
  $("save").addEventListener("click", saveSettings);
  $("package").addEventListener("click", downloadPackage);
  $("load").addEventListener("click", () => $("load-file").click());
  $("load-file").addEventListener("change", (e) => e.target.files[0] && loadSettings(e.target.files[0]));
  const l = initialLang();
  $("lang").value = l;
  setLang(l);
  await loadBoards();
});
