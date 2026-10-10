# Rescue boot mode

If the device fails to boot successfully several times in a row (boot loop), it will automatically enter **Rescue Mode** on the next restart. Rescue Mode provides a minimal Wi-Fi Access Point (AP) and a web API for recovery and debugging, even if the main firmware is broken.

**How Rescue Mode works:**

- The firmware tracks boot failures using the ESP8266's RTC user memory.
- If the device crashes or resets too many times before reaching a stable state, Rescue Mode is triggered.
- In Rescue Mode, the device:
    - Starts a Wi-Fi AP with the same SSID and password as normal operation.
    - Serves a debug web page and a minimal API (no authentication required).
    - Allows you to reset the API token, upload new firmware (OTA), or reboot the device.
    - Displays a debug screen with system info (heap, CPU, screen, version, etc.).

**Rescue API endpoints:**

- `GET /api/v1/rescue/status` — System/debug info (JSON)
- `POST /api/v1/rescue/token` — Reset API token (JSON body: `{ "token": "newtoken" }`)
- `POST /api/v1/rescue/reboot` — Reboot device
- `POST /api/v1/rescue/ota` — Upload new firmware (multipart/form-data)

**When to use Rescue Mode:**

- If the device is stuck in a boot loop or fails to start normally
- If you lose access to the web UI or API due to a misconfiguration
- To recover from a failed firmware update

**How to exit Rescue Mode:**

- After a successful boot and stable operation, the crash counter is reset automatically
- Reboot the device after fixing the issue (e.g., uploading new firmware or resetting the token)

**Recovering the aquarium firmware this way:** the exact commands (how to trigger rescue mode by power-cycling, flashing over the `GeekMagic` access point, and the mandatory reboot call that resets the boot counter) are in [flashing.md](flashing.md#if-something-goes-wrong).
