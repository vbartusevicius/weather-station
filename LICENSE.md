# Licensing

Everything in this repository is open source. Each folder is licensed as
follows; the SPDX identifier links to the full licence text.

| Folder        | Licence                                                          | Contents                                         |
| ------------- | ---------------------------------------------------------------- | ------------------------------------------------ |
| `station/`    | [MIT](https://spdx.org/licenses/MIT.html)                        | ESP32-C6 weather station firmware & Web UI       |
| `simulation/` | [MIT](https://spdx.org/licenses/MIT.html)                        | Synthetic weather physics & DSP integration lab  |
| `docker/`     | [MIT](https://spdx.org/licenses/MIT.html)                        | Container definitions for every tool             |
| `hardware/`   | [CERN-OHL-P-2.0](https://spdx.org/licenses/CERN-OHL-P-2.0.html)  | Circuits, PCBs, releases and hardware tooling    |
| `mechanical/` | [CERN-OHL-P-2.0](https://spdx.org/licenses/CERN-OHL-P-2.0.html)  | Parametric roof enclosure and fit checks         |
| documentation | [CC-BY-4.0](https://spdx.org/licenses/CC-BY-4.0.html)            | `README.md`, `AGENTS.md`, every `docs/` folder   |

Copyright the Roof Weather Station contributors.

## Third-party components

Linked into the firmware image:

| Component | Version | Licence | Source |
| --- | --- | --- | --- |
| espressif/led_strip | 3.0.3 | Apache-2.0 | <https://components.espressif.com/components/espressif/led_strip> |
| ESP-IDF | 5.5.0 (PlatformIO espressif32 6.12.0) | Apache-2.0 and per-component licences | <https://github.com/espressif/esp-idf> |

ESP-IDF ships components under their own licences, among them the FreeRTOS
kernel (MIT), newlib (BSD-style), lwIP (BSD-3-Clause), mbed TLS (Apache-2.0)
and Espressif driver/HAL code (Apache-2.0).

The boards use KiCad's stock symbol, footprint and 3D libraries (CC-BY-SA-4.0
with an exception for designs made with them) and supplier verification data
from the JLCEDA/EasyEDA Official Library (<https://lceda.cn/>, <https://easyeda.com>).
