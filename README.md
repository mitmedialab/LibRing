# LibRing - Liberated Smart Rings for arts and research

A repo to allow reprogramming the firmware of an affordable ring with:
- accelerometer
- touch sensor
- seven segment display
- PPG for SPO2
- heart rate BPM sensor

Functions:
- Streams accelerometer and touch sensor data via Bluetooth from an SR08 ring (down to $10 USD in Alibaba)
- Demos:
   - gesture recognition on accelerometer data for media volume control (see below) 
   - sleep stage monitoring
   - webMidi interface for music control
   - artistic visualization in browser and on the ring screen
   - remote camera controller

## Demo

![Gesture Recognition Demo](./docs/demo/gesture-demo-simple.gif)

## Firmware (custom, flashed over the air)

### Identify the ring variant

The stock OTA receiver rejects an image ("invalid image header") unless the product ID in its header matches the ring's own. To find it, scan with a BLE app (e.g. nRF Connect): the ring's **manufacturer data ends with `000100XX`**, where `XX` is the product ID.

| Manufacturer data ends with | MCU marking (`DA14585 00000...`) | Build |
|---|---|---|
| `0001004C` (rings bought in 2026) | `...2512NCAA`, `...2503NCAA` | `make` (default) |
| `00010017` (older SR08 rings) | `...2451NCAA`, `...1919NCAA`, `...2503NCAA` | `make PRODUCT_ID=17` |

Both variants use the same DA14585 MCU and pinout, so the MCU marking can't tell them apart (lot `2503NCAA` appears in both). Use the manufacturer data.

### Build and flash

```sh
cd Firmware_src/projects/target_apps/ble_examples/prox_reporter
make                  # or: make PRODUCT_ID=17
```

Upload `output.bin` with the Dialog [SUOTA app](https://play.google.com/store/apps/details?id=com.dialog.suota), using the settings in `docs/OTA_Settings_for_apk.jpg`.

Other keeprapid ring/band models use other product IDs, mostly on a different MCU (DA14531/DA14535). Before flashing a ring with an unknown ID, check that its MCU is a DA14585.

## Acknowledgements

This is based on [atc1441/ATC_SR08_Ring](https://github.com/atc1441/ATC_SR08_Ring), sources in `Firmware_src/projects/target_apps/ble_examples/prox_reporter/`.

