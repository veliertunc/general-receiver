# Television, FM radio and Ethernet receiver

This repository contains a Linux receiver service and a reference hardware design. The target is a set-top receiver for **DVB-T, DVB-T2 and DVB-C television**, **analog FM broadcast radio**, and **Ethernet/IP transport streams**. Ethernet also carries the HTTP control and MPEG-TS streaming interface.

## Selected chips and modules

| Function | Selected part | Notes |
| --- | --- | --- |
| Host processor, memory and boot | Raspberry Pi Compute Module 4, 2 GB | Quad-core 64-bit Linux host with hardware video decode support. Use the Lite version with carrier-board eMMC or the eMMC version. |
| Terrestrial/cable RF tuner | Silicon Labs Si2157-A20 on a DVB tuner module | RF tuner; use only a module whose exact tuner and demodulator combination is supported by the target Linux kernel. |
| DVB demodulator | Silicon Labs Si2168-B40 on the same module | DVB-T/T2/C demodulation. Linux has `si2168` and `si2157` media drivers. The module must provide a supported USB or parallel transport-stream connection; USB tuner modules avoid routing a high-speed TS bus on the carrier. |
| FM tuner | Silicon Labs Si4703 | Separate 76–108 MHz FM receiver, I²C control, analog L/R audio output. Use a module with a proper antenna network and audio coupling. |
| Ethernet | CM4 Ethernet MAC plus carrier-board Gigabit Ethernet PHY (Microchip LAN7800 USB-to-Ethernet is an alternative) | CM4 needs a carrier board with PHY, magnetics, RJ45 and ESD protection to expose wired Ethernet. |
| FM audio output | Si4703 analog output to line-level audio jack or amplifier | Add AC coupling, volume/ mute circuitry and an amplifier if a speaker output is required. |
| Storage / OS | CM4 eMMC or carrier-board microSD; Raspberry Pi OS Lite 64-bit | Linux owns DVB, Ethernet, USB, I²C and process scheduling. |

The Si2168 is a DVB demodulator, not the RF tuner; the Si2157 is its matching RF front end. The Si4703 is a distinct FM-only receiver. FM audio comes out as analog audio. This design does not claim DAB, satellite TV (DVB-S/S2), ATSC, DRM, or analog television support. A tuner module is a critical dependency: buy one with documented Si2168/Si2157 silicon, Linux driver support, reference clock/reset arrangement, and a supported USB bridge before laying out a carrier board. Bare QFN chips are not plug-and-play.

The CM4 and Si2168 capabilities are documented by their manufacturers: [Raspberry Pi CM4 product information](https://www.raspberrypi.com/products/compute-module-4/), [Si2168 DVB-T2/T/C product brief](https://www.mouser.com/datasheet/2/472/silcs01678_1-2492605.pdf), and [Linux Si2168 driver](https://github.com/torvalds/linux/blob/master/drivers/media/dvb-frontends/si2168.h). The upstream tuner driver currently reports that Si2157 does not support FM, which is why this design uses a separate Si4703 for FM ([Linux si2157 driver](https://github.com/torvalds/linux/blob/master/drivers/media/tuners/si2157.c)).

## What the firmware does

The software is organized as a package. `firmware/receiver.py` is a small startup entry point; `firmware/receiver_app/main.py` loads configuration; `firmware/receiver_app/api.py` owns HTTP routes; and `firmware/receiver_app/services/` contains the independently callable hardware services:

- `services/television.py` checks the DVB frontend and starts `dvbv5-zap` for a channel stream.
- `services/radio.py` checks and tunes the FM device through V4L2.
- `services/ethernet.py` reports network interfaces and joins multicast groups to receive MPEG-TS.

Configuration is passed into service calls rather than loaded globally, so a subsystem can be imported and used without starting the HTTP server. Each component has a separate file: `config/api.json`, `config/television.json`, `config/radio.json`, and `config/ethernet.json`. The installer places them in `/etc/receiver/`; set `RECEIVER_CONFIG_DIR` to a different directory to load another set. Startup validates each subsystem's settings and identifies the section that needs correction if a value is invalid.

Linux's DVB device API, V4L2 radio API, and Ethernet sockets handle the hardware interface; kernel drivers perform tuner control and demodulation.

- `GET /status` reports whether the DVB frontend and FM radio device are present and lists network interfaces.
- `GET /tv/status`, `/radio/status`, and `/ethernet/status` return status for an individual subsystem.
- `GET /tv.ts?channel=NAME` tunes a named DVB service from `channels.conf` and returns its MPEG transport stream over HTTP.
- `POST /radio/tune` with `{"frequency_mhz": 99.5}` tunes `/dev/radio0`. Audio is available at the Si4703 analog output; this endpoint does not digitize FM audio.
- `GET /udp.ts?group=239.1.1.1&port=1234` joins an IPv4 multicast group on Ethernet and relays the received MPEG-TS datagrams over HTTP.

The selected DVB hardware has one frontend, so serve one tuned broadcast TV service at a time. The service reserves the configured adapter while streaming and returns HTTP 409 for competing TV requests. It releases the reservation and closes the tuner process when a stream ends. This reservation applies within the receiver process; avoid running other tuner applications against the same adapter concurrently.

This is the application firmware for the stated scope, plus a reference device-tree overlay; it is not a TV codec, kernel, tuner module, or production board design. TV picture/audio decoding and HDMI output are provided by the Linux media/player stack on the CM4 (for example, connect a compatible client to the MPEG-TS URL). The HTTP service streams transport streams; it does not implement a web UI, conditional access, service scan UI, recording database, or transcoding. The service has no authentication; keep it on a trusted LAN or add access control before exposing it to an untrusted network.

## Build and install

Check the separate configuration files before deployment with `python firmware/receiver.py --config-dir config --check-config`. This checks settings without opening a network port or accessing receiver hardware. The installer also checks configuration and preserves existing component configuration files during upgrades.

No cross-compiler is needed; the firmware is Python 3 and uses system Linux media tools. On a fresh 64-bit Raspberry Pi OS Lite installation on the CM4 carrier:

1. Connect a supported Si2168/Si2157 DVB-T/T2/C tuner module and a Si4703 FM module. Ensure the FM tuner is enabled on the I²C bus in the device tree and that the kernel exposes it as `/dev/radio0`.
2. For the reference CM4 I/O Board wiring, connect Si4703 SDA/SCL to I²C1 (GPIO2/3), IRQ to GPIO24, and active-low RESET to GPIO17. Use 3.3 V logic. If your module/carrier uses different pins, edit `hardware/receiver-si4703-overlay.dts` before installation. The Linux Si470x I²C driver requires its IRQ input; wire the module's GPIO2/IRQ output.
3. Copy this repository to the device and run `sudo ./install.sh`. It compiles and enables the reference device-tree overlay when the standard Raspberry Pi OS boot configuration is present. Reboot to bind the radio driver.
4. Scan your local DVB network with `dvbv5-scan` and place the resulting dvbv5 channel list at `/etc/receiver/channels.conf`. Channel names in that file are used by the `/tv.ts` endpoint.
5. Check `systemctl status tv-receiver` and open `http://DEVICE_IP:8080/status`.

The installer uses Debian/Raspberry Pi OS packages `dvb-tools`, `v4l-utils`, `linux-firmware`, and `device-tree-compiler`. Some distributions package tuner firmware separately; install any firmware requested by the kernel in the distribution's supported firmware directory. The radio driver needs to create `/dev/radio0`; Linux's Si470x I²C driver needs an interrupt line, configured by the supplied overlay for the reference wiring. The overlay is specific to a BCM2711 CM4 board; use the carrier manufacturer's I²C and GPIO mapping on other boards.

Example calls:

```sh
curl http://DEVICE_IP:8080/status
curl -N 'http://DEVICE_IP:8080/tv.ts?channel=News'
curl -X POST -H 'Content-Type: application/json' \
  -d '{"frequency_mhz":99.5}' http://DEVICE_IP:8080/radio/tune
curl -N 'http://DEVICE_IP:8080/udp.ts?group=239.1.1.1&port=1234'
```

The radio request tunes the station; listen through the line output wired from the Si4703. IPTV multicast must already be present on the selected Ethernet/VLAN and must use MPEG-TS datagrams. Only multicast in `239.0.0.0/8` is accepted by the relay.

## Hardware notes before manufacture

Use the tuner module vendor's reference design for RF matching, SAW filtering, crystal, power rails, clocking, reset, and I²C address selection. Give the TV and FM antennas suitable 50/75-ohm input networks and protect exposed connectors against ESD. Use regulated rails sized for the CM4 peak current, sequence rails as required by each module, and keep switching supplies away from the RF front ends. The carrier must provide the CM4 high-density connectors, Ethernet PHY/magnetics, USB host for the DVB module, I²C and GPIO interrupt/reset lines for Si4703, and audio output circuitry. Verify the local broadcast standard and antenna system before choosing regional band filters or scanning frequencies.

This reference has not been tested on physical hardware. Validate the exact tuner module, Linux kernel drivers, required firmware files and local broadcast parameters on a bench prototype before committing to a production PCB.
