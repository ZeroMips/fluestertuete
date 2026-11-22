#!/usr/bin/env bash
# Install the Flüstertüte CD ripping station on a Raspberry Pi (Raspberry Pi OS Lite, Debian trixie).
#
# Normally run once by the first boot (cloud-init, files from https://fluestertuete.zeromips.org/ripstation.html).
# By hand, as root from this directory, after writing /etc/cd-ripper/station.conf (see setup-station.py):
#     sudo ./install.sh
# Idempotent: safe to run again, e.g. after changing station.conf.
set -euo pipefail
cd "$(dirname "$0")"
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
[ -r /etc/cd-ripper/station.conf ] || { echo "/etc/cd-ripper/station.conf is missing"; exit 1; }

LIB=/usr/local/lib/ripstation
export DEBIAN_FRONTEND=noninteractive

echo "== clock"
# A Pi has no clock battery: on the first boot it starts in the past and apt rejects the archive signatures as
# "not live yet". Wait for the network time (up to 3 minutes), then retry the update until it fully works.
for i in $(seq 1 36); do
    [ "$(timedatectl show -p NTPSynchronized --value 2>/dev/null)" = yes ] && break
    sleep 5
done
date

echo "== packages"
for i in 1 2 3 4 5 6; do
    apt-get update -qq --error-on=any && break
    [ "$i" = 6 ] && { echo "apt-get update keeps failing: check the internet connection and the clock"; exit 1; }
    sleep 20
done
apt-get install -y -qq python3 abcde cdparanoia flac lame id3v2 eject cd-discid libdiscid0 python3-mutagen \
    rsync bluez bluez-alsa-utils libasound2-plugin-bluez mpg123 avahi-daemon
# What the NAS type needs (cifs-utils, nfs-common or davfs2)
NAS_PACKAGES="$(python3 setup-station.py --packages)"
apt-get install -y -qq $NAS_PACKAGES
# Only for the optional hardware panel (cd-ui); Raspberry Pi OS has them, other systems may not
apt-get install -y -qq python3-gpiozero python3-lgpio python3-luma.oled python3-pil python3-smbus2 fonts-dejavu-core \
    i2c-tools || echo "(hardware panel packages missing: fine without the panel)"

echo "== user and groups"
id ripper >/dev/null 2>&1 || useradd --system --create-home --home-dir /var/lib/cd-ripper --shell /usr/sbin/nologin ripper
for g in cdrom i2c gpio audio bluetooth davfs2; do
    if getent group "$g" >/dev/null; then usermod -aG "$g" ripper; fi
done
echo i2c-dev > /etc/modules-load.d/i2c-dev.conf
modprobe i2c-dev || true

echo "== files"
install -d /etc/cd-ripper "$LIB"
for f in abcde.conf kinder-serien.txt; do
    [ -e "/etc/cd-ripper/$f" ] || install -m 0644 "config/$f" "/etc/cd-ripper/$f"
done
install -m 0644 config/cd-ripper.conf "$LIB/cd-ripper.conf"   # the reference of every setting
install -m 0755 setup-station.py "$LIB/"
install -m 0755 bin/cd-rip bin/cd-ui bin/cd-web bin/cd-bt /usr/local/bin/
install -m 0644 udev/99-cd-ripper.rules /etc/udev/rules.d/
install -m 0644 systemd/cd-rip@.service systemd/cd-ui.service systemd/cd-rip-flush.service \
    systemd/cd-rip-flush.timer systemd/cd-web.service systemd/cd-bt.service /etc/systemd/system/
install -m 0644 systemd/cd-ripper.tmpfiles /etc/tmpfiles.d/cd-ripper.conf
systemd-tmpfiles --create /etc/tmpfiles.d/cd-ripper.conf
install -d -o ripper -g ripper /var/lib/cd-ripper /var/lib/cd-ripper/jobs /var/lib/cd-ripper/cddb

echo "== settings and NAS (/mnt/nas)"
python3 "$LIB/setup-station.py"
chgrp ripper /etc/cd-ripper/cd-ripper.conf
systemctl daemon-reload
systemctl restart mnt-nas.automount 2>/dev/null || systemctl restart remote-fs.target || true

echo "== services"
udevadm control --reload
systemctl unmask cd-rip@sr0.service 2>/dev/null || true
systemctl enable --now avahi-daemon.service cd-web.service cd-rip-flush.timer cd-bt.service
systemctl restart cd-web.service cd-bt.service
# Optional hardware panel (LCD/OLED, encoder, buzzer): systemctl enable --now cd-ui.service
echo
echo "Installed. Web page: http://$(hostname).local/   NAS check: ls /mnt/nas"
