// Page texts. Keys are used as data-t attributes in setup.html and by app.js.
export const TEXTS = {
  de: {
    title: "Flüstertüte einrichten",
    tagline: "Hörspiele vom eigenen NAS, kabellos. Ohne Figuren, ohne Cloud.",
    browser_missing: "Dieser Browser kann nicht mit USB-Geräten sprechen. Bitte Chrome oder Edge auf einem Computer verwenden (nicht auf dem Handy).",
    privacy: "Alles passiert in diesem Browser: Passwörter verlassen diesen Computer nur über das USB-Kabel zum Gerät.",
    s_device: "1. Gerät",
    board: "Gerät",
    device_lang: "Sprache auf dem Gerät",
    s_wifi: "2. WLAN",
    wifi_ssid: "WLAN-Name",
    wifi_password: "WLAN-Passwort",
    s_nas: "3. Hörspiele auf dem NAS",
    nas_type: "Zugang",
    type_synology: "Synology-Freigabelink",
    type_webdav: "WebDAV",
    help_synology: "In der File Station den Ordner mit den Hörspielen anklicken, „Teilen“ wählen, ein Passwort setzen und den Link kopieren. Das Gerät kennt dann nur diesen Link, nicht Ihr Konto.",
    help_webdav: "WebDAV gibt es bei fast jedem NAS (Synology: Paket „WebDAV Server“, QNAP, Nextcloud …). Am besten einen eigenen Benutzer anlegen, der nur lesen darf.",
    share_link: "Freigabelink",
    share_link_hint: "z. B. https://192.168.1.10:5001/sharing/AbCdEf (gofile.me-Links funktionieren nicht)",
    share_password: "Passwort des Links",
    nas_url: "Adresse des NAS",
    nas_url_hint: "z. B. https://192.168.1.10:5001",
    dav_url: "WebDAV-Adresse",
    dav_url_hint: "z. B. https://192.168.1.10:5006 oder https://cloud.example.org/remote.php/dav/files/kim",
    dav_user: "Benutzer",
    dav_password: "Passwort",
    root: "Ordner mit den Hörspielen",
    root_hint_synology: "Name des freigegebenen Ordners, mit / davor, z. B. /Hörspiele",
    root_hint_webdav: "Pfad unter der WebDAV-Adresse, z. B. /music/Hörspiele",
    advanced: "Erweitert (Zertifikat)",
    tls_name: "Name im Zertifikat",
    tls_name_hint: "Nur nötig, wenn das NAS per IP erreicht wird, sein Zertifikat aber einen Namen trägt (z. B. meinnas.synology.me).",
    ca_pem: "Eigenes Zertifikat (PEM)",
    ca_pem_hint: "Nur für NAS mit selbst ausgestelltem Zertifikat.",
    dlna_url: "Medienserver (DLNA) für schnelle Aktualisierung",
    dlna_url_hint: "Bei Synology automatisch. Sonst leer lassen: die Bibliothek wird alle 3 Stunden neu eingelesen.",
    s_extras: "4. Extras (optional)",
    extras_hint: "Nur mit der CD-Ripstation auf einem Raspberry Pi (Einrichtung: ripstation.html). Sonst leer lassen.",
    ripper_url: "CD-Ripper",
    ripper_url_hint: "Die CD-Ripstation: http://ripstation.local (oder ihre IP-Adresse, z. B. http://192.168.1.20)",
    ripper_password: "Passwort des CD-Rippers",
    boombox_url: "Boombox (Bluetooth über den Pi)",
    boombox_url_hint: "Leer = Adresse des CD-Rippers mit Port 8081",
    s_flash: "5. Aufs Gerät übertragen",
    flash_hint: "Gerät per USB anschließen und auf „Installieren“ klicken. Danach zeigt das Protokoll, ob WLAN und NAS klappen.",
    erase_all: "Gerät vorher komplett löschen (empfohlen beim ersten Mal; löscht auch gemerkte Hörpositionen)",
    btn_install: "Installieren",
    btn_settings_only: "Nur Einstellungen ändern",
    btn_save: "Einstellungen speichern …",
    btn_load: "Einstellungen laden …",
    save_hint: "Die Datei enthält Ihre Passwörter: gut aufbewahren.",
    firmware: "Firmware",
    no_firmware: "Für dieses Gerät ist keine Firmware vorhanden.",
    missing: "Bitte ausfüllen: ",
    connecting: "Verbinde mit dem Gerät …",
    wrong_chip: "Das angeschlossene Gerät passt nicht zur Auswahl (gefunden: {found}, erwartet: {want}).",
    board_switched: "Angeschlossen ist ein {name}: dafür wird jetzt installiert.",
    erasing: "Lösche …",
    writing: "Schreibe {file} … {pct} %",
    done_flash: "Übertragen. Das Gerät startet neu.",
    watching: "Das Gerät meldet sich …",
    log_settings_ok: "Einstellungen gelesen",
    log_settings_bad: "Einstellungen nicht verwendbar",
    log_wifi_ok: "WLAN verbunden ({ip})",
    log_wifi_notfound: "WLAN nicht gefunden: Name prüfen, Gerät näher an den Router?",
    log_wifi_password: "WLAN-Anmeldung abgelehnt: Passwort prüfen",
    log_wifi_other: "WLAN getrennt (Grund {reason})",
    log_nas_ok: "NAS erreicht und angemeldet",
    log_nas_refused: "NAS lehnt die Anmeldung ab: Link-Passwort bzw. Benutzer/Passwort prüfen",
    log_nas_notfound: "Ordner auf dem NAS nicht gefunden: Pfad prüfen",
    log_cert: "Zertifikat des NAS nicht akzeptiert: Adresse bzw. „Name im Zertifikat“ prüfen",
    log_library: "Bibliothek bereit ({n} Einträge)",
    log_lost: "Verbindung zum Gerät beendet. Auf dem Display geht es weiter.",
    error: "Fehler: {msg}",
    cancelled: "Kein Gerät ausgewählt.",
    source: "Quellcode",
    imprint: "Impressum",
    privacy_page: "Datenschutz",
    offline: "Ohne Chrome oder Edge",
    offline_hint: "Firefox und Safari erlauben Webseiten keinen Zugriff auf USB-Geräte. Dann gibt es hier ein Paket mit Firmware, Ihren Einstellungen und einer Anleitung für das Flash-Programm esptool. Auch das entsteht nur in diesem Browser.",
    btn_package: "Paket herunterladen",
    guide: `Flüstertüte für {board} ({version})

Die Datei settings.bin enthält Ihre Passwörter: nicht weitergeben, nach dem Flashen löschen.

1. Python 3 installieren (python.org), dann im Terminal:
     python -m pip install esptool

2. Gerät per USB anschließen. Der Anschluss (PORT unten) heißt
     Windows:  COM3, COM4 … (siehe Geräte-Manager, „Anschlüsse“)
     macOS:    /dev/cu.usbserial-…  oder  /dev/cu.usbmodem…
     Linux:    /dev/ttyUSB0  oder  /dev/ttyACM0
{tdeck_hint}
3. Im Ordner mit diesen Dateien ausführen:

   Komplett installieren:
     {install}

   Vorher alles löschen (beim ersten Mal empfohlen, löscht auch gemerkte Hörpositionen):
     {erase}

   Nur die Einstellungen ändern (Hörpositionen bleiben):
     {settings}

Danach startet das Gerät neu und verbindet sich mit WLAN und NAS.
`,
    guide_tdeck: "\n   T-Deck: Klappt die Verbindung nicht, beim Einstecken den Trackball gedrückt halten.\n",
  },
  en: {
    title: "Set up Flüstertüte",
    tagline: "Stories from your own NAS, wireless. No figurines, no cloud.",
    browser_missing: "This browser cannot talk to USB devices. Please use Chrome or Edge on a computer (not on a phone).",
    privacy: "Everything happens in this browser: passwords leave this computer only through the USB cable to the device.",
    s_device: "1. Device",
    board: "Device",
    device_lang: "Language on the device",
    s_wifi: "2. Wi-Fi",
    wifi_ssid: "Wi-Fi name",
    wifi_password: "Wi-Fi password",
    s_nas: "3. Stories on the NAS",
    nas_type: "Access",
    type_synology: "Synology share link",
    type_webdav: "WebDAV",
    help_synology: "In File Station, select the folder with the stories, choose “Share”, set a password and copy the link. The device then only knows this link, not your account.",
    help_webdav: "Almost every NAS offers WebDAV (Synology: package “WebDAV Server”, QNAP, Nextcloud …). Best create a user of its own that may only read.",
    share_link: "Share link",
    share_link_hint: "e.g. https://192.168.1.10:5001/sharing/AbCdEf (gofile.me links do not work)",
    share_password: "Link password",
    nas_url: "NAS address",
    nas_url_hint: "e.g. https://192.168.1.10:5001",
    dav_url: "WebDAV address",
    dav_url_hint: "e.g. https://192.168.1.10:5006 or https://cloud.example.org/remote.php/dav/files/kim",
    dav_user: "User",
    dav_password: "Password",
    root: "Folder with the stories",
    root_hint_synology: "Name of the shared folder, with / in front, e.g. /Stories",
    root_hint_webdav: "Path below the WebDAV address, e.g. /music/Stories",
    advanced: "Advanced (certificate)",
    tls_name: "Name in the certificate",
    tls_name_hint: "Only needed when the NAS is reached by IP but its certificate carries a name (e.g. mynas.synology.me).",
    ca_pem: "Own certificate (PEM)",
    ca_pem_hint: "Only for a NAS with a self-signed certificate.",
    dlna_url: "Media server (DLNA) for quick updates",
    dlna_url_hint: "Automatic on Synology. Otherwise leave empty: the library is rescanned every 3 hours.",
    s_extras: "4. Extras (optional)",
    extras_hint: "Only with the CD ripping station on a Raspberry Pi (set up at ripstation.html). Otherwise leave empty.",
    ripper_url: "CD ripper",
    ripper_url_hint: "The CD ripping station: http://ripstation.local (or its IP address, e.g. http://192.168.1.20)",
    ripper_password: "CD ripper password",
    boombox_url: "Boombox (Bluetooth through the Pi)",
    boombox_url_hint: "Empty = the CD ripper's address with port 8081",
    s_flash: "5. Transfer to the device",
    flash_hint: "Connect the device by USB and click “Install”. The log then shows whether Wi-Fi and NAS work.",
    erase_all: "Erase the device completely first (recommended the first time; also erases remembered positions)",
    btn_install: "Install",
    btn_settings_only: "Change settings only",
    btn_save: "Save settings …",
    btn_load: "Load settings …",
    save_hint: "The file contains your passwords: keep it safe.",
    firmware: "Firmware",
    no_firmware: "There is no firmware for this device.",
    missing: "Please fill in: ",
    connecting: "Connecting to the device …",
    wrong_chip: "The connected device does not match the selection (found: {found}, expected: {want}).",
    board_switched: "A {name} is connected: installing for that one.",
    erasing: "Erasing …",
    writing: "Writing {file} … {pct} %",
    done_flash: "Transferred. The device restarts.",
    watching: "Waiting for the device …",
    log_settings_ok: "Settings read",
    log_settings_bad: "Settings not usable",
    log_wifi_ok: "Wi-Fi connected ({ip})",
    log_wifi_notfound: "Wi-Fi not found: check the name, device closer to the router?",
    log_wifi_password: "Wi-Fi login refused: check the password",
    log_wifi_other: "Wi-Fi disconnected (reason {reason})",
    log_nas_ok: "NAS reached and logged in",
    log_nas_refused: "NAS refuses the login: check the link password or user/password",
    log_nas_notfound: "Folder not found on the NAS: check the path",
    log_cert: "NAS certificate not accepted: check the address or “Name in the certificate”",
    log_library: "Library ready ({n} entries)",
    log_lost: "Connection to the device ended. The display takes it from here.",
    error: "Error: {msg}",
    cancelled: "No device selected.",
    source: "Source code",
    imprint: "Legal notice (German)",
    privacy_page: "Privacy (German)",
    offline: "Without Chrome or Edge",
    offline_hint: "Firefox and Safari do not let websites access USB devices. Then you get a package here with the firmware, your settings and a guide for the flashing tool esptool. It is made in this browser, too.",
    btn_package: "Download package",
    guide: `Flüstertüte for {board} ({version})

settings.bin contains your passwords: do not pass it on, delete it after flashing.

1. Install Python 3 (python.org), then in a terminal:
     python -m pip install esptool

2. Connect the device by USB. Its port (PORT below) is called
     Windows:  COM3, COM4 … (see Device Manager, "Ports")
     macOS:    /dev/cu.usbserial-…  or  /dev/cu.usbmodem…
     Linux:    /dev/ttyUSB0  or  /dev/ttyACM0
{tdeck_hint}
3. Run in the folder with these files:

   Full install:
     {install}

   Erase everything first (recommended the first time, also erases remembered positions):
     {erase}

   Change the settings only (positions stay):
     {settings}

The device then restarts and connects to Wi-Fi and the NAS.
`,
    guide_tdeck: "\n   T-Deck: if it does not connect, hold the trackball pressed while plugging it in.\n",
  },
};

export function format(text, vars = {}) {
  return text.replace(/\{(\w+)\}/g, (_, k) => (k in vars ? vars[k] : `{${k}}`));
}
