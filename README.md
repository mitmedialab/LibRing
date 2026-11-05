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

Flash it from the computer over BLE (Linux only for now, needs `pip install bleak`):

```sh
make scan                                       # list nearby rings, strongest signal first
make flash                                      # build, then flash the nearest ring (~50 s)
make flash FLASH_ARGS="--address 48:23:35:F4:00:10"
make flash FLASH_ARGS="--all --min-rssi -60"    # every ring close to the computer, one by one
```

- Stock rings show their product ID in `make scan` and are skipped if it doesn't match the build. Flashed rings advertise as `LibRing_XX`, XX = the last byte of their BLE address (the web apps accept any `LibRing*` name, and `TEST` from older firmware).
- Each ring keeps its BLE address: `make flash` writes the address the ring advertises (the factory one for a stock ring) into the image. `--set-address AA:BB:...` gives one ring a specific address.
- A ring connected to another app (e.g. a web demo) stops advertising: disconnect it first.
- If the connection keeps dropping, raise Linux's BLE supervision timeout from 420 ms to 4 s (once):
  ```sh
  sudo sed -i 's/^#ConnectionSupervisionTimeout=$/ConnectionSupervisionTimeout=400/' /etc/bluetooth/main.conf
  sudo systemctl restart bluetooth
  ```
- More options (`--name`, `--product-id`, `--retries`, `--chunk`...): `python3 Eclipse/suota_flash.py --help`.

Or upload with the Dialog [SUOTA app](https://play.google.com/store/apps/details?id=com.dialog.suota), using the settings in `docs/OTA_Settings_for_apk.jpg`. To keep each ring's BLE address, upload its own image, made by `make images` (rings in range) or `make images FLASH_ARGS="--set-address AA:BB:..."`: `output_<ADDRESS>.bin`. With a plain `output.bin`, the firmware gives each ring an address made from the unique ID of its SPI flash chip (`48:23:35:XX:YY:ZZ`); if the flash chip has none, the ring keeps the shared default address `48:23:35:F4:00:10` and gets a random name at each boot.

Other keeprapid ring/band models use other product IDs, mostly on a different MCU (DA14531/DA14535). Before flashing a ring with an unknown ID, check that its MCU is a DA14585.

## Acknowledgements

This is based on [atc1441/ATC_SR08_Ring](https://github.com/atc1441/ATC_SR08_Ring), sources in `Firmware_src/projects/target_apps/ble_examples/prox_reporter/`.

