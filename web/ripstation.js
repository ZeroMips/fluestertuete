// CD ripping station setup page: form -> three cloud-init files for the SD card of a fresh Raspberry Pi OS Lite,
// as one ZIP. Everything happens in this browser; the station's code comes from this site (ripstation.tar.gz).
import { metaData, networkConfig, userData, validate } from "./ripstation-config.js";
import { makeZip } from "./zip.js";

const $ = (id) => document.getElementById(id);

const TEXTS = {
  de: {
    title: "CD-Ripstation einrichten",
    tagline: "CD rein, fertig: ein Raspberry Pi liest Ihre CDs ein und legt sie aufs NAS.",
    privacy: "Alles passiert in diesem Browser: die Dateien entstehen hier und landen nur auf Ihrer Speicherkarte.",
    s_need: "1. Was Sie brauchen",
    need_pi: "Einen Raspberry Pi 4 oder 5 mit Netzteil (getestet mit dem Pi 5).",
    need_drive: "Ein USB-CD-Laufwerk. Am Pi 5 am besten eines mit eigenem Netzteil oder an einem aktiven USB-Hub.",
    need_card: "Eine microSD-Karte ab 8 GB und auf dem Computer den kostenlosen Raspberry Pi Imager.",
    need_nas: "Ein NAS mit einer Freigabe, in die die Station schreiben darf (SMB, NFS oder WebDAV).",
    need_speaker: "Wer mag: eine Bluetooth-Boombox, über die die Flüstertüte dann spielen kann.",
    s_net: "2. Netzwerk",
    net_hint: "Ohne WLAN-Angaben braucht der Pi ein Netzwerkkabel.",
    wifi_ssid: "WLAN-Name",
    wifi_password: "WLAN-Passwort",
    wifi_country: "Land (für die erlaubten WLAN-Kanäle)",
    s_nas: "3. Das NAS",
    nas_type: "Zugang",
    type_smb: "SMB (Windows-Freigabe)",
    type_nfs: "NFS",
    type_webdav: "WebDAV",
    help_smb: "Geht mit fast jedem NAS (Synology: Systemsteuerung › Dateidienste › SMB). Der Benutzer muss in die Freigabe schreiben dürfen.",
    help_nfs: "Für Fortgeschrittene: Die Freigabe muss die IP-Adresse des Pi mit Schreibrecht zulassen (Synology: Squash „Alle Benutzer zu admin zuordnen“). Kein Benutzer, kein Passwort.",
    help_webdav: "Für Nextcloud, Synology „WebDAV Server“ und andere. Der Benutzer muss schreiben dürfen.",
    url_smb: "Server und Freigabe",
    url_nfs: "Server und Export",
    url_webdav: "WebDAV-Adresse",
    hint_smb: "z. B. //192.168.1.10/music oder //meinnas/music",
    hint_nfs: "z. B. 192.168.1.10:/volume1/music",
    hint_webdav: "z. B. https://cloud.example.org/remote.php/dav/files/kim",
    nas_user: "Benutzer",
    nas_password: "Passwort",
    s_dirs: "4. Ordner",
    dirs_hint: "Ordner innerhalb der Freigabe, in die die Station die CDs sortiert.",
    dir_kids: "Kinderhörspiele",
    dir_kids_hint: "Derselbe Ordner, den die Flüstertüte spielt: dann sind neue CDs sofort dort.",
    dir_spoken: "Hörbücher",
    dir_music: "Musik",
    dir_unsorted: "Unsortiert (CDs, die niemand kennt)",
    s_more: "5. Weitere Einstellungen",
    lang: "Sprache der Station",
    web_password: "Passwort für die Station (optional)",
    web_password_hint: "Schützt die Seite der Station. Dasselbe Passwort dann bei der Flüstertüte als „Passwort des CD-Rippers“ eintragen. Leer lassen geht auch: die Station ist nur im Heimnetz zu sehen.",
    advanced: "Für Bastler",
    gnudb_contact: "E-Mail-Adresse für GnuDB",
    gnudb_hint: "GnuDB ist eine freie CD-Datenbank mit vielen deutschen Hörspielen, die MusicBrainz nicht kennt. Sie fragt nach einer E-Mail-Adresse. Leer = GnuDB wird nicht gefragt.",
    ssh_key: "Öffentlicher SSH-Schlüssel",
    ssh_hint: "Für eine Anmeldung am Pi als Benutzer „ripstation“. Ohne Schlüssel gibt es keine Anmeldung; nötig ist sie nicht.",
    s_files: "6. Auf die Speicherkarte",
    btn_download: "Dateien herunterladen (ZIP)",
    step_imager: "Raspberry Pi Imager starten, Ihr Pi-Modell wählen, als Betriebssystem „Raspberry Pi OS (other)“ › „Raspberry Pi OS Lite (64-bit)“, dann die Karte. Die angebotenen Anpassungen (Name, WLAN, Benutzer) nicht verwenden: das erledigen unsere Dateien. Schreiben lassen.",
    step_copy: "Karte aus- und wieder einstecken. Sie erscheint als Laufwerk „bootfs“. Das ZIP entpacken und die drei Dateien user-data, network-config und meta-data dorthin kopieren; vorhandene ersetzen.",
    step_boot: "Karte in den Pi stecken, CD-Laufwerk anschließen, einschalten. Beim ersten Start richtet sich die Station selbst ein: etwa 10 Minuten, mit Internet.",
    step_open: "Dann im Browser http://ripstation.local öffnen. CD einlegen, fertig. Dort lässt sich auch die Boombox koppeln.",
    step_player: "Die Flüstertüte zeigt die Station und spielt über die Boombox, wenn bei ihrer Einrichtung als CD-Ripper http://ripstation.local eingetragen ist.",
    btn_player: "Flüstertüte einrichten →",
    missing: "Bitte ausfüllen: ",
    p_smb_form: "Die SMB-Adresse sieht so aus: //server/freigabe",
    p_nfs_form: "Die NFS-Adresse sieht so aus: server:/pfad",
    p_webdav_form: "Die WebDAV-Adresse beginnt mit https:// (oder http://)",
    p_short: "Das WLAN-Passwort ist kürzer als 8 Zeichen: so kurz sind WLAN-Passwörter nie.",
    p_ssh_form: "Das sieht nicht wie ein öffentlicher SSH-Schlüssel aus (ssh-ed25519 …).",
    preparing: "Erstelle die Dateien …",
    no_code: "Die Software der Station konnte nicht geladen werden ({msg}).",
    done: "Heruntergeladen: {name}. Weiter mit den Schritten unten.",
    guide_name: "ANLEITUNG.txt",
    source: "Quellcode",
    imprint: "Impressum",
    privacy_page: "Datenschutz",
  },
  en: {
    title: "Set up the CD ripping station",
    tagline: "Insert a CD, done: a Raspberry Pi rips your CDs and puts them on the NAS.",
    privacy: "Everything happens in this browser: the files are made here and only end up on your memory card.",
    s_need: "1. What you need",
    need_pi: "A Raspberry Pi 4 or 5 with its power supply (tested with the Pi 5).",
    need_drive: "A USB CD drive. On a Pi 5 preferably one with its own power supply, or on a powered USB hub.",
    need_card: "A microSD card of 8 GB or more, and the free Raspberry Pi Imager on your computer.",
    need_nas: "A NAS with a share the station may write to (SMB, NFS or WebDAV).",
    need_speaker: "If you like: a Bluetooth boombox Flüstertüte can then play through.",
    s_net: "2. Network",
    net_hint: "Without Wi-Fi details the Pi needs a network cable.",
    wifi_ssid: "Wi-Fi name",
    wifi_password: "Wi-Fi password",
    wifi_country: "Country (for the allowed Wi-Fi channels)",
    s_nas: "3. The NAS",
    nas_type: "Access",
    type_smb: "SMB (Windows share)",
    type_nfs: "NFS",
    type_webdav: "WebDAV",
    help_smb: "Works with almost every NAS (Synology: Control Panel › File Services › SMB). The user must be allowed to write to the share.",
    help_nfs: "For experts: the share must allow the Pi's IP address with write access (Synology: squash “Map all users to admin”). No user, no password.",
    help_webdav: "For Nextcloud, Synology “WebDAV Server” and others. The user must be allowed to write.",
    url_smb: "Server and share",
    url_nfs: "Server and export",
    url_webdav: "WebDAV address",
    hint_smb: "e.g. //192.168.1.10/music or //mynas/music",
    hint_nfs: "e.g. 192.168.1.10:/volume1/music",
    hint_webdav: "e.g. https://cloud.example.org/remote.php/dav/files/kim",
    nas_user: "User",
    nas_password: "Password",
    s_dirs: "4. Folders",
    dirs_hint: "Folders inside the share that the station sorts the CDs into.",
    dir_kids: "Children's stories",
    dir_kids_hint: "The same folder Flüstertüte plays: then new CDs show up there at once.",
    dir_spoken: "Audiobooks",
    dir_music: "Music",
    dir_unsorted: "Unsorted (CDs nobody knows)",
    s_more: "5. More settings",
    lang: "Language of the station",
    web_password: "Password for the station (optional)",
    web_password_hint: "Protects the station's page. Enter the same password for Flüstertüte as “CD ripper password”. Empty works too: the station is only visible on your home network.",
    advanced: "For tinkerers",
    gnudb_contact: "E-mail address for GnuDB",
    gnudb_hint: "GnuDB is a free CD database with many German audio dramas MusicBrainz lacks. It asks for an e-mail address. Empty = GnuDB is not asked.",
    ssh_key: "Public SSH key",
    ssh_hint: "To log in to the Pi as user “ripstation”. Without a key there is no login; you don't need one.",
    s_files: "6. Onto the memory card",
    btn_download: "Download the files (ZIP)",
    step_imager: "Start Raspberry Pi Imager, choose your Pi model, as operating system “Raspberry Pi OS (other)” › “Raspberry Pi OS Lite (64-bit)”, then the card. Do not use the customisation it offers (name, Wi-Fi, user): our files do that. Let it write.",
    step_copy: "Take the card out and put it back in. It shows up as a drive called “bootfs”. Unpack the ZIP and copy the three files user-data, network-config and meta-data onto it, replacing the ones there.",
    step_boot: "Put the card into the Pi, connect the CD drive, switch on. On its first start the station sets itself up: about 10 minutes, with internet.",
    step_open: "Then open http://ripstation.local in your browser. Insert a CD, done. That page also pairs the boombox.",
    step_player: "Flüstertüte shows the station and plays through the boombox when its setup has http://ripstation.local as CD ripper.",
    btn_player: "Set up Flüstertüte →",
    missing: "Please fill in: ",
    p_smb_form: "An SMB address looks like this: //server/share",
    p_nfs_form: "An NFS address looks like this: server:/path",
    p_webdav_form: "The WebDAV address starts with https:// (or http://)",
    p_short: "The Wi-Fi password is shorter than 8 characters: Wi-Fi passwords never are.",
    p_ssh_form: "That does not look like a public SSH key (ssh-ed25519 …).",
    preparing: "Making the files …",
    no_code: "The station software could not be loaded ({msg}).",
    done: "Downloaded: {name}. Continue with the steps below.",
    guide_name: "README.txt",
    source: "Source code",
    imprint: "Imprint (German)",
    privacy_page: "Privacy (German)",
  },
};

let lang = "de";
const t = (key, vars = {}) => (TEXTS[lang][key] ?? TEXTS.de[key] ?? key).replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? "");

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

const nasType = () => document.querySelector("input[name=nas_type]:checked").value;

function showNasType() {
  const type = nasType();
  $("nas-help").textContent = t(`help_${type}`);
  $("nas_url-label").textContent = t(`url_${type}`);
  $("nas_url-hint").textContent = t(`hint_${type}`);
  $("nas_url").placeholder = t(`hint_${type}`).replace(/^(z\. B\.|e\.g\.) /, "").split(" ")[0];
  $("nas-login").hidden = type === "nfs";
}

const FIELDS = ["wifi_ssid", "wifi_password", "wifi_country", "nas_url", "nas_user", "nas_password", "dir_kids",
  "dir_spoken", "dir_music", "dir_unsorted", "lang", "web_password", "gnudb_contact", "ssh_key"];

function collect() {
  const s = { nas_type: nasType(), timezone: Intl.DateTimeFormat().resolvedOptions().timeZone || "Europe/Berlin" };
  for (const f of FIELDS) s[f] = $(f).value.trim();
  if (s.nas_type === "nfs") s.nas_user = s.nas_password = "";
  return s;
}

function status(text, kind = "info") {
  const li = Object.assign(document.createElement("li"), { className: kind, textContent: text });
  $("status").replaceChildren(li);
}

function explain(problems) {
  for (const el of document.querySelectorAll("input.invalid, textarea.invalid")) el.classList.remove("invalid");
  for (const p of problems) $(p.field)?.classList.add("invalid");
  const missing = problems.filter((p) => p.problem === "missing").map((p) => {
    const label = p.field === "nas_url" ? `url_${nasType()}` : p.field;
    return t(label);
  });
  const other = problems.filter((p) => p.problem !== "missing").map((p) => t(`p_${p.problem}`));
  return [missing.length ? t("missing") + missing.join(", ") : "", ...other].filter(Boolean).join(" ");
}

function guide() {
  return [t("title"), "", ...["step_imager", "step_copy", "step_boot", "step_open"].map((k, i) => `${i + 1}. ${t(k)}`), "",
    t("step_player"), "", "https://fluestertuete.zeromips.org/ripstation.html", ""].join("\n");
}

function base64(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

async function download() {
  const s = collect();
  const problems = validate(s);
  if (problems.length) return status(explain(problems), "bad");
  explain([]);
  status(t("preparing"));
  let code;
  try {
    const r = await fetch("ripstation/ripstation.tar.gz", { cache: "no-cache" });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    code = base64(new Uint8Array(await r.arrayBuffer()));
  } catch (e) {
    return status(t("no_code", { msg: e.message }), "bad");
  }
  const id = "ripstation-" + Array.from(crypto.getRandomValues(new Uint8Array(6)), (b) => b.toString(16).padStart(2, "0")).join("");
  const zip = makeZip([
    { name: "user-data", data: userData(s, code) },
    { name: "network-config", data: networkConfig(s) },
    { name: "meta-data", data: metaData(id) },
    { name: t("guide_name"), data: guide().replace(/\n/g, "\r\n") },  // readable in Notepad
  ]);
  const name = "ripstation.zip";
  const a = Object.assign(document.createElement("a"), { href: URL.createObjectURL(zip), download: name });
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  status(t("done", { name }), "ok");
}

document.addEventListener("DOMContentLoaded", () => {
  for (const b of document.querySelectorAll("[data-lang]")) b.addEventListener("click", () => setLang(b.dataset.lang));
  for (const r of document.querySelectorAll("input[name=nas_type]")) r.addEventListener("change", showNasType);
  $("download").addEventListener("click", download);
  const l = initialLang();
  $("lang").value = l;
  setLang(l);
});
