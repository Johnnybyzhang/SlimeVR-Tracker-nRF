#!/usr/bin/env python3
"""Execute extracted production core paths with deterministic device failures."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'harness/python'))
from c_extract import extract_block

HERE = Path(__file__).resolve().parent
SRC = Path(os.environ.get("SOURCE_ROOT", HERE.parents[2])) / "src"


def function(text, name):
    return extract_block(text, rf"^(?:static )?(?:void|bool|uint8_t) {re.escape(name)}\([^;{{]*\)\s*\{{")


system = (SRC / "system/system.c").read_text()
status = (SRC / "system/status.c").read_text()
build = (SRC / "build_defines.h").read_text()
parts = [line for line in build.splitlines() if line.startswith("#define BUILD_TIMESTAMP ")]
if len(parts) != 1:
    raise ValueError("Missing timestamp definition")
parts += [function(system, name) for name in (
    "reboot_counter_read", "reboot_counter_write", "button_read", "button_read_filtered",
    "dock_read", "chg_read", "stby_read", "button_interrupt_handler")]
parts += [function(status, "status_update")]
# Supply the optional board configuration for extracted button paths.
button_config = """
#ifndef CONFIG_USER_BUTTON_REBOOT
#define CONFIG_USER_BUTTON_REBOOT 1
#endif
#ifndef USER_SHUTDOWN_ENABLED
#define USER_SHUTDOWN_ENABLED 0
#endif
#ifndef IS_ENABLED
#define IS_ENABLED(value) (value)
#endif
""" + re.search(r"^#define BUTTON_HOLD_ACTION .*?$", system, re.MULTILINE).group() + "\n"
parts.insert(0, button_config)
with tempfile.TemporaryDirectory(prefix="tracker-system-core-") as directory:
    temp = Path(directory)
    (temp / "production.inc").write_text("\n\n".join(parts))
    binary = temp / "test"
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-Wno-unused-variable", "-g", "-O1", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-DCONFIG_CONNECTION_RAW_COLLECTION=1", "-DCONFIG_SENSOR_USE_MAGNETOMETER=1",
        "-I", str(temp),
        "-I", str(SRC), str(HERE / "test.c"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
