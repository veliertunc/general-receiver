# C television, FM radio and Ethernet receiver firmware

The application is written entirely in C11. It targets a **Raspberry Pi Compute Module 4 with embedded Linux**, boots from eMMC or microSD, and receives DVB-T/T2/C television, FM radio, and IPv4 multicast MPEG transport streams over Ethernet. Python and external tuner programs are no longer runtime dependencies.

This is an embedded Linux firmware project. Its deployable artifact is a disk image containing the boot firmware, Linux kernel, drivers, filesystem and compiled C receiver. It is not a bare-metal MCU binary. Linux drivers and vendor demodulator firmware remain necessary; rewriting the application in C does not replace them.

## Hardware to use

| Function | Selected hardware | Connection / reason |
| --- | --- | --- |
| Compute module | CM4, BCM2711 SoC, 2 GB RAM, 16 GB eMMC, without wireless (CM4002016) | Enough resources for Linux media drivers and transport streams; eMMC is the firmware storage. Use a CM4 Lite with microSD during prototyping if preferred. |
| Prototype carrier | Official CM4 IO Board | Provides power, USB host, Ethernet connector, HDMI, GPIO and boot access. Follow its documented power input requirements. |
| Television receiver | Hauppauge WinTV-soloHD, European DVB-T/T2/C version | A concrete USB tuner supported by Linux. Verify the exact hardware revision/USB ID before purchasing; US ATSC products are different. |
| TV silicon reference | Si2168 demodulator, Si2157-family RF tuner, eMPIA EM28xx USB bridge | Supported driver families; use the assembled USB tuner rather than a custom bare-chip RF board. Actual tuner silicon/firmware varies by product revision. |
| FM receiver | Si4703 breakout exposing SDA, SCL, reset and GPIO2/interrupt | I2C control, FM antenna input, analog stereo output. A board exposing only I2C without IRQ is unsuitable for the supplied Linux overlay. |
| Ethernet PHY | Broadcom BCM54210PE already on CM4 | The carrier supplies magnetics/RJ45 and protection. An additional Ethernet PHY is not needed. This corrects the earlier hardware table. |
| FM audio | Si4703 analog L/R into powered speakers or an external line-level amplifier | CM4 has no built-in analog audio input. FM streaming would require an added ADC/USB sound interface and a capture implementation. |

Sources: [CM4 datasheet](https://datasheets.raspberrypi.com/cm4/cm4-datasheet.pdf), [Hauppauge Linux tuner support](https://support.hauppauge.com/pages/support/support_linux.html), [Linux EM28xx device list](https://cdn.kernel.org/doc/html/latest/admin-guide/media/em28xx-cardlist.html), [Si470x device-tree binding](https://code.googlesource.com/linux/torvalds/linux/%2B/793582ff47f8b73be8d3d925d750bf3ef79f33c7/Documentation/devicetree/bindings/media/silabs%2Csi470x.yaml).

The RF standard assumed here is DVB-T/T2/C and analog FM. Satellite, ATSC, DAB, encrypted channels and analog television are outside this implementation. TV output is a complete multiplex transport stream over HTTP; a player selects and decodes a service from it. Local HDMI viewing requires a separately configured media player. The receiver does not implement video codecs or a display UI.

## C source layout

| File | Responsibility |
| --- | --- |
| `firmware/c/main.c` | Bounded HTTP server, request parsing, streaming, shutdown |
| `firmware/c/config.c` | Configuration defaults, parsing and validation |
| `firmware/c/television.c` | DVBv5 tuning, lock timeout, demux/DVR stream, exclusive tuner ownership |
| `firmware/c/radio.c` | V4L2 FM tuning and unmute; retains the radio handle to keep audio powered |
| `firmware/c/ethernet.c` | Multicast membership and interface enumeration |
| `firmware/c/receiver.h` | Shared C interfaces |
| `buildroot/` | External package, kernel fragment and image integration |

There is no Python source in the application. Shell scripts handle building, installation and boot startup.

## Build the C application

On Linux with GCC or Clang, CMake, POSIX threads and Linux media headers:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/receiver --config config/receiver.conf --check-config
```

Run `sh tools/check-host.sh` on Linux for strict C compilation and HTTP/configuration checks without RF hardware. These checks do not validate the kernel drivers, RF lock, FM audio, or boot image.

On Raspberry Pi OS Lite 64-bit, run `sudo sh install.sh` from this repository to build and install the C binary and systemd service. Existing `/etc/receiver` configuration files are preserved. If an earlier Python installation exists, the new systemd unit replaces its startup command; the legacy `/opt/tv-receiver` files are unused.

To cross-compile the application with an installed GNU AArch64 Linux toolchain, configure CMake with `-DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake`. Match the target's C library/sysroot; the binary alone does not include Linux, drivers or boot firmware. Buildroot supplies its own matched cross toolchain when building the image.

## Radio wiring and kernel configuration

For the supplied `hardware/receiver-si4703-overlay.dts`, wire Si4703 SDA/SCL to CM4 GPIO2/3, active-low RESET to GPIO17, and the chip's GPIO2/IRQ output to CM4 GPIO24. Use 3.3 V logic, a common ground and the breakout vendor's supply/antenna/audio instructions. Ensure the module starts in two-wire I2C mode at address 0x10; its strap/reset requirements depend on the breakout.

On Raspberry Pi OS, compile the overlay:

```sh
sudo dtc -@ -I dts -O dtb -o /boot/firmware/overlays/receiver-si4703.dtbo hardware/receiver-si4703-overlay.dts
```

Add the following under an `[all]` section in `/boot/firmware/config.txt`, then reboot:

```ini
dtparam=i2c_arm=on
dtoverlay=receiver-si4703
otg_mode=1
```

The kernel must enable the Si470x I2C radio driver. The Buildroot kernel fragment requests it. On a prebuilt OS, check the kernel configuration and install/build the driver if absent. The overlay alone cannot add a missing kernel driver. Confirm `/dev/radio0` and `/dev/dvb/adapter0/frontend0` appear before expecting reception.

## Configuration and reception

`config/receiver.conf` is installed at `/etc/receiver/receiver.conf`. It configures API address/port, DVB adapter, radio device and FM band, multicast interface address, and the tuning/idle timeout. The default HTTP port is 8080, maximum active clients is 16, and multicast is restricted to 239.0.0.0/8. Keep the service on a trusted LAN; authentication and TLS are not implemented.

`/etc/receiver/channels.conf` now uses a simple C-native format, **not the earlier dvbv5 channel-file format**:

```text
# name delivery_system frequency_Hz bandwidth_Hz symbol_rate
News DVB-T2 514000000 8000000 0
Cable DVB-C 450000000 8000000 6900000
```

These are examples, not verified local broadcast frequencies. Each name identifies a multiplex. Replace the entries with the frequency, bandwidth and symbol rate used by your broadcaster. DVB-T/T2 uses automatic modulation; DVB-C uses Annex A and automatic QAM detection, which depends on driver support. Explicit PLP selection, channel scanning and service PID filtering are not implemented.

```sh
curl http://DEVICE_IP:8080/status
curl http://DEVICE_IP:8080/ethernet/status
curl -N 'http://DEVICE_IP:8080/tv.ts?channel=News'
curl -X POST -H 'Content-Type: application/json' -d '{"frequency_mhz":99.5}' http://DEVICE_IP:8080/radio/tune
curl -N 'http://DEVICE_IP:8080/udp.ts?group=239.1.1.1&port=1234'
```

The FM request accepts a JSON object containing only `frequency_mhz`; audio comes from the radio board's analog output. Concurrent TV requests receive 409. Signal acquisition failure returns 503 before streaming begins. An established stream closes after the configured idle timeout; stalled clients have send/receive timeouts. Multicast input must carry plain MPEG-TS, not RTP-encapsulated TS.

## Build a flashable image

Use a Linux build host with Buildroot's documented dependencies. The external tree layers onto the upstream `raspberrypi4_64_defconfig`, which includes CM4 device trees. Pin and record the Buildroot release/commit used for a product build; this integration has not yet completed a full image build.

From a checked-out Buildroot tree:

```sh
sh /path/to/television-receiver/tools/build-image.sh /path/to/television-receiver
```

The intended output is `output/images/sdcard.img`. The image recipe adds the C application, BusyBox init startup, CM4 boot selection, radio overlay and media driver configuration. Ethernet uses the upstream board's DHCP setup. The recipe is based on the [upstream Pi 4 64-bit configuration](https://github.com/buildroot/buildroot/blob/master/configs/raspberrypi4_64_defconfig); verify selected kernel symbols and generated boot files against your pinned Buildroot version.

**Vendor firmware is required for many USB tuner revisions.** Obtain the exact blobs named by the kernel from the tuner manufacturer's Linux support page, and place them under `buildroot/board/overlay/lib/firmware/` before building. They are not generated from this application's C source and are not bundled here. Check redistribution terms before shipping an image containing them. Missing blobs allow the OS to boot but prevent TV reception.

For a CM4 Lite, write `sdcard.img` to a microSD card using Raspberry Pi Imager's custom-image option. For an eMMC CM4, fit the CM4 IO Board's nRPIBOOT jumper, connect the documented USB slave port, run Raspberry Pi's `rpiboot` to expose eMMC, and write the custom image to that exposed device. Remove the boot jumper and power-cycle afterward. Writing an image erases the selected storage; verify the device before flashing. Follow [Raspberry Pi's eMMC flashing instructions](https://www.raspberrypi.com/documentation/computers/compute-module.html).

The project currently supplies C source and image build inputs, **not a built or hardware-validated image**. Verify kernel driver selection, tuner firmware loading, TV signal lock, FM analog output, DHCP/multicast and reboot behavior on the selected hardware before treating an image as production firmware.
