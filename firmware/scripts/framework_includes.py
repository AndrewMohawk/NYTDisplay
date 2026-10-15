"""Make Arduino 3.x's core Networking headers visible to its WiFi library.

The pinned vendor core omits a `depends=Networking` declaration in WiFi's
library.properties. PlatformIO's conditional dependency scanner therefore
does not pass Network/src to WiFi's own compiler, even though it builds the
Networking library. Add only this existing framework include directory;
do not modify downloaded packages or substitute a third-party Network library.
"""
from pathlib import Path

Import("env")  # Provided by PlatformIO/SCons.
framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
network = Path(framework) / "libraries" / "Network" / "src"
filesystem = Path(framework) / "libraries" / "FS" / "src"
if not (network / "Network.h").is_file():
    raise RuntimeError("Pinned Arduino core is missing its Networking library")
if not (filesystem / "FS.h").is_file():
    raise RuntimeError("Pinned Arduino core is missing its FS library")
env.Append(CPPPATH=[str(network), str(filesystem)])
