# Flashing this firmware onto a GeekMagic SmallTV-Ultra

No cables, no soldering: everything goes over WiFi. Tested on a SmallTV-Ultra
shipped with stock firmware V9.0.50. You need Docker (or PlatformIO) to build,
or the two `.bin` files from someone who built them.

## 1. Build

```bash
cd firmware
docker run --rm -v "$(pwd):/workspace" -w /workspace \
  -v "$(pwd)/.pio:/tmp/.platformio" -e PLATFORMIO_CORE_DIR=/tmp/.platformio \
  -u "$(id -u):$(id -g)" ghcr.io/times-z/devcontainer:latest \
  /home/debian/.platformio/penv/bin/pio run
```

Run the same command again with `pio run --target buildfs` at the end to
build the filesystem image. Results:

- `.pio/build/esp12e/firmware.bin`
- `.pio/build/esp12e/littlefs.bin` (web UI, config, the aquarium animation)

Before building the filesystem image, copy `data/config-smartTV.example` to
`data/config.json` and set:

- `api_token`: any long random string, e.g. `openssl rand -hex 24`
- `animation_file`: the animation shown after boot, `aquarium.pxa` is bundled

`data/config.json` is git-ignored because it holds the token.

## 2. Flash the firmware through the stock firmware

Find the device IP in the stock web UI or your router, then:

```bash
curl -F "firmware=@.pio/build/esp12e/firmware.bin" http://<device-ip>/update
```

The device reboots into this firmware. It has no WiFi credentials yet, so it
opens an access point: SSID `GeekMagic`, password `$str0ngPa$$w0rd`.

## 3. Flash the filesystem

Connect your computer to the `GeekMagic` access point, then:

```bash
curl -F "filesystem=@.pio/build/esp12e/littlefs.bin" http://192.168.4.1/legacyupdate
```

(This route only exists while the filesystem is empty.) After the reboot the
token from `config.json` is stored in the device's secure storage and the API
requires it from then on. The aquarium starts playing.

## 4. Connect to your WiFi

Open `http://192.168.4.1`, go to the token page and paste your `api_token`
(without `Bearer`), then configure your WiFi. The device joins your network
and the access point disappears. Find its new IP in your router.

## 5. Check

```bash
curl -H "Authorization: Bearer <token>" http://<device-ip>/api/v1/system/info
curl -H "Authorization: Bearer <token>" http://<device-ip>/api/v1/screen
```

## Later updates

With the token in place, new firmware goes straight to the running device:

```bash
curl -H "Authorization: Bearer <token>" -F "file=@.pio/build/esp12e/firmware.bin" http://<device-ip>/api/v1/ota/fw
```

## If something goes wrong

Unplug and re-plug USB-C three times within 20 seconds. The display turns red
("RESCUE MODE") and the device opens the `GeekMagic` access point again:

```bash
curl -F "firmware=@firmware.bin" http://192.168.4.1/api/v1/rescue/ota
curl -X POST http://192.168.4.1/api/v1/rescue/reboot   # resets the boot counter, required
```

## Your own animations

The ESP8266 cannot decode GIFs (not enough RAM for the decoder), so animations
use the PXA format: a small palette plus raw frames on a coarse grid, scaled
up on the device. `tools/pxa.py` creates them (needs Python 3 with Pillow and
NumPy):

```bash
python3 tools/pxa.py aquarium --frames 192 --fps 24     # the bundled aquarium
python3 tools/pxa.py from-gif my.gif --grid 60 --scale 4 # convert a small GIF
python3 tools/pxa.py to-gif data/gif/aquarium.pxa       # render back to check
```

Upload a file to the device and show it. `"persist": true` makes it the
animation shown after the next boot:

```bash
curl -H "Authorization: Bearer <token>" -F "file=@my.pxa" http://<device-ip>/api/v1/gif
curl -X POST -H "Authorization: Bearer <token>" -H "Content-Type: application/json" \
  -d '{"name":"animation","file":"my.pxa","persist":true}' http://<device-ip>/api/v1/screen
```

Keep an eye on `freeBytes` in `GET /api/v1/gif`: a 60x60 animation costs
3.6 KB per frame, the filesystem has about 1.3 MB for animations.
