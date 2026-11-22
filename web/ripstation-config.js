// The files for a Raspberry Pi OS Lite card that make it a Flüstertüte CD ripping station on its first boot
// (cloud-init, NoCloud on the boot partition). Plain functions: the page uses them, and tests/run.sh checks them
// against tests/ripstation_vector.json and cloud-init's own schema.
//
// settings: {lang, wifi_ssid, wifi_password, wifi_country, nas_type, nas_url, nas_user, nas_password,
//            dir_kids, dir_spoken, dir_music, dir_unsorted, gnudb_contact, web_password, ssh_key, timezone}

// YAML accepts JSON strings as double-quoted scalars: every value goes in that way, nothing can break out.
const q = (s) => JSON.stringify(String(s ?? ""));
const oneLine = (s) => String(s ?? "").replace(/[\r\n]+/g, " ").trim();

// /etc/cd-ripper/station.conf, read by ripstation/setup-station.py.
export function stationConf(s) {
  const lines = [
    "# Written by the Flüstertüte setup page. Change it and run: sudo /usr/local/lib/ripstation/setup-station.py",
    `LANG=${s.lang === "en" ? "en" : "de"}`,
    `NAS_TYPE=${oneLine(s.nas_type)}`,
    `NAS_URL=${oneLine(s.nas_url)}`,
    `NAS_USER=${oneLine(s.nas_user)}`,
    `NAS_PASSWORD=${oneLine(s.nas_password)}`,
    `DIR_KIDS=${oneLine(s.dir_kids)}`,
    `DIR_SPOKEN=${oneLine(s.dir_spoken)}`,
    `DIR_MUSIC=${oneLine(s.dir_music)}`,
    `DIR_UNSORTED=${oneLine(s.dir_unsorted)}`,
    `GNUDB_CONTACT=${oneLine(s.gnudb_contact)}`,
    `WEB_PASSWORD=${oneLine(s.web_password)}`,
  ];
  return lines.join("\n") + "\n";
}

// network-config: Wi-Fi if given, the cable always (whichever is plugged in).
export function networkConfig(s) {
  const out = [
    "network:",
    "  version: 2",
    "  ethernets:",
    "    eth0:",
    "      dhcp4: true",
    "      optional: true",
  ];
  if (oneLine(s.wifi_ssid)) {
    out.push(
      "  wifis:",
      "    wlan0:",
      "      dhcp4: true",
      "      optional: true",
      `      regulatory-domain: ${q(oneLine(s.wifi_country || "DE").toUpperCase())}`,
      "      access-points:",
      `        ${q(oneLine(s.wifi_ssid))}:`,
      `          password: ${q(oneLine(s.wifi_password))}`,
    );
  }
  return out.join("\n") + "\n";
}

// meta-data: a new instance id makes cloud-init treat the card as new even if it ran on it before.
export function metaData(instanceId) {
  return `instance-id: ${q(instanceId)}\nlocal-hostname: "ripstation"\n`;
}

// user-data: system settings, the station code (base64 tar.gz) and its settings, then the installer.
export function userData(s, codeBase64) {
  const en = s.lang === "en";
  const key = oneLine(s.ssh_key);
  const out = [
    "#cloud-config",
    "# Flüstertüte CD ripping station: set up on the first boot (about 10 minutes, needs the internet).",
    "hostname: ripstation",
    "manage_etc_hosts: true",
    `timezone: ${q(oneLine(s.timezone) || "Europe/Berlin")}`,
    `locale: ${q(en ? "en_GB.UTF-8" : "de_DE.UTF-8")}`,
    // An admin account only for those who give an SSH key; everyone else never needs a login.
    "users:",
    "  - name: ripstation",
    "    gecos: Flüstertüte CD ripping station",
    "    groups: [sudo, adm]",
    "    shell: /bin/bash",
    "    lock_passwd: true",
    "    sudo: ALL=(ALL) NOPASSWD:ALL",
  ];
  if (key) out.push("    ssh_authorized_keys:", `      - ${q(key)}`);
  out.push(
    `enable_ssh: ${key ? "true" : "false"}`,
    "ssh_pwauth: false",
    "write_files:",
    "  - path: /etc/cd-ripper/station.conf",
    "    permissions: '0600'",
    `    content: ${q(stationConf(s))}`,
    "  - path: /usr/local/src/ripstation.tar.gz",
    "    permissions: '0644'",
    "    encoding: b64",
    `    content: ${codeBase64}`,
    "runcmd:",
    "  - [sh, -c, 'mkdir -p /usr/local/src/ripstation && tar -xzf /usr/local/src/ripstation.tar.gz -C /usr/local/src/ripstation']",
    "  - [sh, -c, '/usr/local/src/ripstation/install.sh > /var/log/ripstation-install.log 2>&1']",
    // The card's boot partition is readable by anyone who holds the card: take the passwords off it.
    "  - [sh, -c, 'for f in /boot/firmware/user-data /boot/firmware/user-data.txt; do [ -e \"$f\" ] && printf \"#cloud-config\\n# Set up on the first boot; settings now in /etc/cd-ripper/station.conf\\n\" > \"$f\"; done; true']",
  );
  return out.join("\n") + "\n";
}

// Every check the page makes before writing files: [{field, problem}] in English keys the page translates.
export function validate(s) {
  const problems = [];
  const need = (field) => { if (!oneLine(s[field])) problems.push({ field, problem: "missing" }); };
  need("nas_url");
  if (!["smb", "nfs", "webdav"].includes(s.nas_type)) problems.push({ field: "nas_type", problem: "missing" });
  const url = oneLine(s.nas_url);
  if (url && s.nas_type === "smb" && !/^(smb:)?(\/\/|\\\\)?[^/\\]+[/\\][^/\\]+/i.test(url)) problems.push({ field: "nas_url", problem: "smb_form" });
  if (url && s.nas_type === "nfs" && !/^[^:/]+:\/./.test(url)) problems.push({ field: "nas_url", problem: "nfs_form" });
  if (url && s.nas_type === "webdav" && !/^https?:\/\//i.test(url)) problems.push({ field: "nas_url", problem: "webdav_form" });
  if (s.nas_type !== "nfs") need("nas_user");
  if (oneLine(s.wifi_ssid) && oneLine(s.wifi_password) && oneLine(s.wifi_password).length < 8)
    problems.push({ field: "wifi_password", problem: "short" });
  if (oneLine(s.ssh_key) && !/^(ssh-|ecdsa-|sk-)/.test(oneLine(s.ssh_key))) problems.push({ field: "ssh_key", problem: "ssh_form" });
  return problems;
}
