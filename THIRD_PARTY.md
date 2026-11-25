# Third-party code and services

| Name | Used for | License |
| --- | --- | --- |
| [libnx](https://github.com/switchbrew/libnx) | Switch system services, console, input | ISC |
| [nlohmann/json](https://github.com/nlohmann/json) 3.12.0 | JSON parsing, vendored in `lib/nlohmann/` | MIT, see `lib/nlohmann/LICENSE.MIT` |
| [curl](https://curl.se/) (devkitPro `switch-curl`) | HTTPS downloads, using the Switch's own SSL service | curl license |
| [libarchive](https://www.libarchive.org/) (devkitPro `switch-libarchive`) | zip, 7z and rar extraction | BSD 2-Clause |
| [stb_truetype 1.28 and stb_image 2.34](https://github.com/nothings/stb) | Font rendering and JPEG/PNG decoding, vendored in `lib/stb/` | MIT or public domain, see `lib/stb/LICENSE` |
| Nintendo Switch system fonts | Text and button icons, loaded from the console at runtime, not included | Nintendo |
| [certifi](https://github.com/certifi/python-certifi) 2026.11.20 (Mozilla CA list) | Root certificates in `romfs/certs.pem`, picked by `tools/make_certs.py` | MPL-2.0 |
| [Pillow](https://python-pillow.org/) | Icon generation script only, not part of the app | MIT-CMU |
| [GameBanana](https://gamebanana.com/) | Mod listings and downloads through its public apiv11 API | Mods belong to their authors |

The GameBanana endpoints ModGrab calls were worked out with help from [SimpleModDownloader](https://github.com/PoloNX/SimpleModDownloader) by PoloNX. No code from it is included.
