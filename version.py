import os
import subprocess

Import("env")

# Single source of truth for the firmware version: the git tag.
# The release workflow sets FIRMWARE_VERSION explicitly; local/CI builds fall
# back to `git describe` so dev binaries report something meaningful.
version = os.environ.get("FIRMWARE_VERSION")
if not version:
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            text=True,
        ).strip()
    except Exception:
        version = "dev"

if version.startswith("v"):
    version = version[1:]

env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])
print("Firmware version: " + version)
