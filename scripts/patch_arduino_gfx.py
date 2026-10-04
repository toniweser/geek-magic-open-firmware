"""PlatformIO pre-build hook: move Arduino_GFX's YCbCr lookup tables to flash.

YCbCr2RGB.h declares ~6.8 KB of `static const` tables for JPEG colour
conversion. On the ESP8266 plain const data lives in DRAM, and the tables are
always linked because Arduino_DataBus::writeYCbCrPixels() is virtual. This
firmware never decodes JPEGs, yet those 6.8 KB were the difference between
uploads working and the TCP stack starving (free heap ~22 KB before).

Adding PROGMEM relocates the tables to flash. The one function that reads
them is never called here, so the byte-access semantics do not matter.
Idempotent: a file that already carries PROGMEM is left alone.
"""
import pathlib
import re

Import("env")  # noqa: F821 - provided by PlatformIO's SCons environment

LIBDEPS = pathlib.Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")  # noqa: F821
TARGET = LIBDEPS / "GFX Library for Arduino" / "src" / "YCbCr2RGB.h"


def patch():
    if not TARGET.exists():
        print(f"[patch_arduino_gfx] {TARGET} not found (library not installed yet?)")
        return
    text = TARGET.read_text()
    if "PROGMEM" in text:
        return
    patched, count = re.subn(r"^(static const (?:u?int16_t) \w+\[\]) = \{", r"\1 PROGMEM = {", text, flags=re.M)
    if count == 0:
        print("[patch_arduino_gfx] no tables matched, header layout changed?")
        return
    TARGET.write_text("#include <pgmspace.h>\n" + patched)
    print(f"[patch_arduino_gfx] moved {count} YCbCr tables to PROGMEM in {TARGET.name}")


patch()
