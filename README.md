# LibRing - Liberated Smart Rings for arts and research

Open firmware and web apps for an affordable smart ring (SR08, down to $10 USD on Alibaba), turning it into a Bluetooth sensor and display for research and artistic projects. The ring has:
- an accelerometer (SC7A20)
- a capacitive touch sensor
- a 5-digit seven-segment LED display, plus a red (heart) and a green (steps) indicator on the outside
- an optical heart-rate / SpO2 sensor (PPG, TYHX HX3918 with green, red and infrared LEDs)

With this firmware, the ring streams its sensors over Bluetooth Low Energy (BLE) to web apps, which run in Chrome or Edge with no installation.

![Gesture Recognition Demo](./docs/demo/gesture-demo-simple.gif)

## Live demos

**[mitmedialab.github.io/LibRing](https://mitmedialab.github.io/LibRing/)**: open a demo, click Connect and choose your ring (`LibRing_XX`). The heart-rate page needs firmware with PPG support (newer than v0.2).

| Demo | What it does |
|---|---|
| Gesture Recognition | Recognizes circles drawn with the hand, in real time |
| Sleep Recognition | Classifies movement and resting states |
| Camera Controller | Takes pictures with the webcam when the ring is touched |
| Web MIDI Interface | Sends the accelerometer X/Y/Z as MIDI control changes, and the touch as a note (or CC) |
| Perlin Noise Visualizer | Drives a Perlin noise field with the ring's tilt |
| Heart Rate, HRV & SpO2 (experimental) | Pulse waveform, heart rate computed on the ring, heart rate variability, session trends, an uncalibrated SpO2 estimate, and the ring's temperature |
| Webcam Pulse (experimental) | Heart rate from the laptop webcam, as an independent check of the ring (no ring needed) |

## Install the firmware (from a phone)

1. On an Android phone, download the latest binary from the [releases page](https://github.com/mitmedialab/LibRing/releases/latest):
   - `LibRing_xxxxxxx_0x4C_2026_rings.bin` for rings bought in 2026
   - `LibRing_xxxxxxx_0x17_older_rings.bin` for older SR08 rings

   Not sure? Try `0x4C` first: a stock ring rejects the wrong one ("invalid image header") without harm, then use the other. Rings already running LibRing accept both.
2. Install the Renesas [SUOTA app](https://play.google.com/store/apps/details?id=com.dialog.suota), close the ring's vendor app, and select the ring (`SR08` for a stock ring, `LibRing_XX` after the first update).
3. Choose the downloaded `.bin`, discard the 1st error/warning, and the **default settings are normally OK** ([screenshot](./docs/OTA_Settings_for_apk.jpg)): block size `240`, image bank `OLDEST`, memory type `SPI`, MISO `P0_5`, MOSI `P0_6`, CS `P0_3`, SCK `P0_0`.
4. Tap **Send to device** (~1 min): the ring restarts as `LibRing_XX`, ready for the demos.

To build the firmware yourself or flash many rings from a computer, see [Firmware](#firmware-custom-flashed-over-the-air).

## Using the ring

- **Display**: by default, a "fluid" that flows with the ring's tilt. Hold the touch sensor for 3 s to switch to the ring ID (the 2 hex digits of its name), hold again to turn the display off, again to go back to the fluid. Holding for 10 s resets the ring.
- **Indicators**: the red heart is lit while an app uses the heart-rate sensor, the green steps while an app streams the accelerometer.
- **Charging**: dim red heart while charging, dim green once the battery is nearly full (also with the display off).
- **Name**: each ring advertises as `LibRing_XX`, XX = the last byte of its BLE address (shown by the "ring ID" display mode).

## Web apps (development)

The apps are in `apps/`. Web Bluetooth needs Chrome or Edge (desktop or Android) and a secure origin (`https://` or `localhost`).

- Plain HTML apps (`ppg`, `rppg`, `camera`, `perlin`, `homepage`): serve the `apps/` folder, e.g. `cd apps && python3 -m http.server 8000`, then open http://localhost:8000/ppg/.
- Vite apps (`gesture-recognition`, `music-app`, `sleep-recognition`): `npm install`, then `npm run dev` in the app's folder (it prints the URL, e.g. http://localhost:3000/LibRing/gesture-recognition/).
- Shared code: `apps/shared/plot.js` (the line charts of all the apps).

The GitHub Pages site is built and deployed by `.github/workflows/deploy.yml` on each push to `main`.

## BLE interface

Custom service `00112233-4455-6677-8899-aabbccddeeff`, characteristics:

| Characteristic | UUID | Content |
|---|---|---|
| Accelerometer | `f2909165-4ce5-8da2-4c10-8b38c19f65cc` | notify, ~11 Hz: X, Y, Z (int16, big endian; `>> 4` gives mg / 8 at ±16 g), range code (byte 6, 3 = ±16 g) |
| Touch | `368d553d-154a-1aa8-3f46-2e87b5e5cd54` | notify: 1 = touched, 0 = released |
| PPG | `e7da0718-b8b6-44b6-912c-e0bfb05d562d` | notify, 10 packets/s: raw sensor samples (24.2 Hz) and ring-computed heart rate, SpO2, motion level. Layout: `ppg_read()` in `src/ppg/HX3918.h`; commands (heart-rate / SpO2 mode, LED control, register access): `user_ppg_config()` in `src/user_proxr.c` |
| Control point | `2d86686a-53dc-25b3-0c4a-f0e10c8dee20` | write |

Subscribing to a characteristic starts its sensor; unsubscribing or disconnecting stops it. Python examples (with `bleak`): `Eclipse/ppg_stream.py`, `Eclipse/ppg_ctl.py`.

## Firmware (custom, flashed over the air)

Sources: `Firmware_src/projects/target_apps/ble_examples/prox_reporter/` (Dialog SDK 6, DA14585). Releases with a ready-to-flash binary: [releases page](https://github.com/mitmedialab/LibRing/releases).

### Identify the ring variant

The stock OTA receiver rejects an image ("invalid image header") unless the product ID in its header matches the ring's own. To find it, scan with a BLE app (e.g. nRF Connect): the ring's **manufacturer data ends with `000100XX`**, where `XX` is the product ID. `make scan` shows it too.

| Manufacturer data ends with | MCU marking (`DA14585 00000...`) | Build |
|---|---|---|
| `0001004C` (rings bought in 2026) | `...2512NCAA`, `...2503NCAA` | `make` (default) |
| `00010017` (older SR08 rings) | `...2451NCAA`, `...1919NCAA`, `...2503NCAA` | `make PRODUCT_ID=17` |

Both variants use the same DA14585 MCU and pinout, so the MCU marking can't tell them apart (lot `2503NCAA` appears in both). Use the manufacturer data. Only the stock firmware checks the product ID: rings already running this firmware accept any image.

### Build and flash

Requires `arm-none-eabi-gcc` (the Makefile defaults to `/opt/arm-none-eabi/bin/`, override with `make CROSS_PREFIX=`).

```sh
cd Firmware_src/projects/target_apps/ble_examples/prox_reporter
make                  # or: make PRODUCT_ID=17
```

Flash it from the computer over BLE (Linux or macOS, needs `pip install bleak`):

```sh
make scan                                       # nearby rings, strongest signal first, with their firmware
make flash                                      # build, then flash the nearest ring
make flash LibRing_C7 LibRing_72                # these rings, by name
make flash FLASH_ARGS="--address 48:23:35:F4:00:10"
make flash_all                                  # every ring in range, one after the other
make flash_all FLASH_ARGS="--min-rssi -60"      # only the rings close to the computer
```

- `make scan` shows each ring's firmware: `commit 1a2b3c4` (the git commit it was built from, `+` if built with uncommitted changes), `stock`, or `-` (older builds).
- Stock rings show their product ID in `make scan` and are skipped if it doesn't match the build.
- Each ring keeps its BLE address: `make flash` writes the address the ring advertises (the factory one for a stock ring) into the image, except the addresses the firmware derives itself (`48:23:35:XX:YY:ZZ`). `--set-address AA:BB:...` gives one ring a specific address.
- A ring connected to another app (e.g. a web demo) stops advertising: disconnect it first.
- macOS hides BLE addresses (rings show as UUIDs), so a ring's address can't be kept: it gets one derived from its flash chip (`48:23:35:XX:YY:ZZ`), as with a plain `output.bin` in the Android app. Rings already on that address (most rings running this firmware) keep it and their name. To flash a release binary without building: `python3 Eclipse/suota_flash.py LibRing_xxxxxxx_0x4C_2026_rings.bin --ring LibRing_C7`.
- More options (`--name`, `--product-id`, `--raw`, `--retries`, `--chunk`...): `python3 Eclipse/suota_flash.py --help`.

Or upload with the Dialog [SUOTA app](https://play.google.com/store/apps/details?id=com.dialog.suota), using the settings in `docs/OTA_Settings_for_apk.jpg`. To keep each ring's BLE address, upload its own image, made by `make images` (rings in range) or `make images FLASH_ARGS="--set-address AA:BB:..."`: `output_<ADDRESS>.bin`. With a plain `output.bin`, the firmware gives each ring an address made from the unique ID of its SPI flash chip (`48:23:35:XX:YY:ZZ`); if the flash chip has none, the ring keeps the shared default address `48:23:35:F4:00:10` and gets a random name at each boot.

Other keeprapid ring/band models use other product IDs, mostly on a different MCU (DA14531/DA14535). Before flashing a ring with an unknown ID, check that its MCU is a DA14585.

## Troubleshooting

- **A stock ring that was used with the vendor app** (e.g. KeepFit) refuses the computer ("Unlikely Error", then disconnections), even after unbinding it in the app. Flash it once from the Android SUOTA app; after that, `make flash` works.
- **The connection keeps dropping while flashing**: raise Linux's BLE supervision timeout from 420 ms to 4 s (once):
  ```sh
  sudo sed -i 's/^#ConnectionSupervisionTimeout=$/ConnectionSupervisionTimeout=400/' /etc/bluetooth/main.conf
  sudo systemctl restart bluetooth
  ```
- **After flashing, `make scan` still shows the old firmware**: BlueZ caches the advertising data, `bluetoothctl remove <address>` clears it.
- **macOS: the script stops right after "Scanning..." (exit code 134)**: macOS killed it because the app running the terminal has no Bluetooth permission. Run it from Terminal or iTerm and allow Bluetooth when asked (System Settings > Privacy & Security > Bluetooth); terminals embedded in some apps can't get it.
- **Web apps, Chrome on Linux**: after a disconnection, Chrome stops delivering the ring's notifications to the page until it is reloaded. The accelerometer apps detect it and read the values instead (same rate); other pages need a reload to reconnect.

## Hardware notes

- Pinout: `docs/Pinout_SR08_by_ATC1441.txt` (with the LED matrix layout and the indicators).
- PPG sensor: TYHX HX3918 (chip ID `0x27`, I2C `0x44`, shared bus with the accelerometer). The LEDs are driven by the sensor: green on phase 2 for the heart rate; red (phase 1) and IR (phase 3) in SpO2 mode.
- SpO2 is experimental and not calibrated: most of the red/IR light reaches the sensor inside the ring without going through the finger, the web app measures this leak once per ring (ring off the finger, in the dark). It is not a medical measurement.
- Temperature: none of the chips has a usable temperature sensor (the DA14585's ADC has no temperature channel; the SC7A20 accelerometer, WHO_AM_I `0x11`, doesn't implement the LIS2DH one). The firmware uses the microcontroller's RC16M oscillator instead, counted against the crystal every second (the radio's own temperature proxy): fewer counts when warmer, 5 to 8 counts per °C depending on the ring (4 rings measured), with very different counts from one ring to another, so each ring needs its own references, ideally 2 (the web app keeps them; 4 rings come calibrated). It measures the ring itself, not the skin.

## Acknowledgements

This is based on [atc1441/ATC_SR08_Ring](https://github.com/atc1441/ATC_SR08_Ring) (firmware sources and pinout). The HX3918 register map and gain control follow the vendor driver published in [HK-bruc1/jl_ac701N_ear](https://github.com/HK-bruc1/jl_ac701N_ear); the stock BLE protocol is documented in the [Gadgetbridge KeepFit pull request](https://codeberg.org/Freeyourgadget/Gadgetbridge/pulls/5326).
