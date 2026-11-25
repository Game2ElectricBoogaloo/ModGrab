# ModGrab

A Nintendo Switch homebrew app that downloads mods from GameBanana and installs them straight into Atmosphere's mod folders. It tracks what it installed, so it can check for updates and cleanly uninstall.

## Features

- Proper graphical interface with the Switch's own system font and button icons: a game grid with icons, mod lists with thumbnails, a mod page with a big preview, and touch screen support.
- Lists the games installed on your Switch, with their version.
- Links each game to its GameBanana page automatically, or lets you pick when the name doesn't match exactly (press X on the mod list to change it later).
- Browses and searches mods, shows every file a mod has with size and upload date.
- Downloads with a progress bar (B cancels) and checks the size against what GameBanana reports.
- Extracts zip, 7z and rar archives and sorts files into the right place:
  - `atmosphere/...` goes to `sdmc:/atmosphere/...`
  - `contents/<titleid>/...` and `exefs_patches/...` go under `sdmc:/atmosphere/`
  - `romfs/` and `exefs/` folders go to `sdmc:/atmosphere/contents/<titleid>/`
  - Smash Ultimate mods go to `sdmc:/ultimate/mods/` for ARCropolis
- When an archive has several option folders (like "Option A/romfs" and "Option B/romfs") it asks which one to install.
- Game version check. GameBanana has no field for which game version a mod targets, so ModGrab reads the versions mentioned in the file name, file description, mod name and the version notes in the mod description, and compares them with your installed game. The mod screen shows the result for the selected file, and a mismatch makes the install prompt ask "install anyway?".
- Remembers which game version each mod was installed on. If the game updates later, the Installed screen flags the mod so you know it might break.
- Installed mods screen (Y on the game list): check every mod for updates at once, open a mod to install its newer file, turn mods on and off, or uninstall.
- Backups and layering. Any file a mod replaces, whether it came from another mod or you copied it by hand, is backed up first and put back when that mod is uninstalled or turned off. Mods can be stacked and removed in any order.
- Overlap warnings. Before installing, ModGrab lists the installed mods whose files the new one replaces.
- Turning a mod off parks its files in `switch/ModGrab/disabled/` and turning it back on puts them back, so you can test which mod breaks something without redownloading.
- Presets. Save which mods are on for a game and switch between setups in one go.
- Download queue. Queue files with Y and keep browsing while they download, then install them from the Downloads screen.
- Local files. Drop .zip, .7z or .rar mods into `switch/ModGrab/local/` (with DBI, ftpd or a card reader) and install them with the same sorting, backups and uninstall support, no internet needed.
- Mod details. A screenshot gallery and the full description.
- Self-updater. Checks GitHub for a newer ModGrab release on launch, verifies the download's SHA-256 against the checksum GitHub publishes, and swaps the .nro in place.
- Checks the Switch's date on launch, since a wrong date makes every secure connection fail.
- Never writes Atmosphere's own files (package3, config, stratosphere and so on) or system title folders.

## Controls

| Screen | Buttons |
| --- | --- |
| Games | A browse mods, Y installed mods, X local files, - downloads, + exit |
| Mod list | A open, L/R page, Y search, X change GameBanana game, B back |
| Mod | A install selected file, Y queue it, X uninstall, - details and gallery, B back |
| Details | Left/Right or L/R switch images, Up/Down scroll the description, B back |
| Installed | A open, Y check updates, - turn on/off, X uninstall, + presets, B back |
| Downloads | A install (or retry a failed one), X remove or cancel, B back |
| Local files | A install, Y refresh, B back |

You can also tap: tap an item to select it, tap it again to open it, and tap the button hints at the bottom.

Search uses the system keyboard, which only works when ModGrab runs as a full app. Launch it through a game with title override (hold R while starting a game) instead of the album.

## Install

Copy `ModGrab.nro` to `sdmc:/switch/ModGrab/ModGrab.nro` and launch it from the Homebrew Menu.

ModGrab keeps its data in `sdmc:/switch/ModGrab/`:

- `state.json` holds which GameBanana page each game is linked to and every file each mod installed.
- `downloads/` is temporary. Archives are deleted after install, except ones with a layout ModGrab doesn't recognize, which stay there so you can install them by hand.
- `backups/<mod id>/` holds files a mod replaced, `disabled/<mod id>/` holds the files of mods that are turned off. Don't edit these by hand.
- `local/` is where you put archives to install offline. ModGrab never deletes them.

## Building

Needs [devkitPro](https://devkitpro.org/wiki/Getting_Started) with these packages:

```
dkp-pacman -S switch-dev switch-curl switch-libarchive switch-pkg-config
```

`switch-libarchive` pulls in zlib, liblzma, zstd, bzip2, lz4 and expat. Then:

```
make
```

The output is `ModGrab.nro`.

### Releasing

The self-updater reads `cfg::UpdateRepo` in `include/config.hpp`. Bump `APP_VERSION` in the Makefile, build, and publish a GitHub release whose tag is the version (like `v1.2.0`) with `ModGrab.nro` attached. GitHub records a SHA-256 for every uploaded asset, and ModGrab refuses any download that doesn't match it.

To regenerate `icon.jpg`:

```
pip install -r requirements.txt
python tools/make_icon.py icon.jpg
```

## Troubleshooting

- **"couldn't reach the server"**: the network is blocking the Switch or needs a proxy. ModGrab uses the proxy set in the Switch's wifi settings (System Settings > Internet > your network > Change Settings > Proxy). School and work networks often block homebrew traffic even when the site opens on a phone.
- **"the Switch's DNS is blocking this site"**: something on the Switch (atmosphere/hosts or a custom DNS) sends gamebanana.com to 127.0.0.1.
- **certificate errors**: the Switch only trusts a short list of certificate authorities. ModGrab adds the common ones from `romfs/certs.pem` (made with `python tools/make_certs.py`), and still checks every certificate. If a site moves to an authority that isn't in that list, add its root to `ROOTS` in the script and rebuild.

## Known limits

- GameBanana's MD5 checksums aren't checked (libnx has no MD5), only the file size.
- Mods packed in a layout without `romfs`, `exefs`, `exefs_patches`, `contents` or `atmosphere` folders can't be placed automatically.
- Update checks compare the mod's version string with what you installed and look for files uploaded after you installed.
- The game version check depends on the mod author writing the version somewhere. "No game version listed" means ModGrab couldn't tell, not that the mod is fine.
- exefs_patches (.ips) only load on the exact build they were made for. ModGrab can't read a game's build ID, so it can't verify those.
- There's no sort or category filter on the mod list. GameBanana's API ignored the sort and filter parameters in testing, so they were left out rather than shipped broken.
- Mods installed with an older ModGrab (before backups existed) don't have backups, so uninstalling them only deletes their own files.

## Credits

See [THIRD_PARTY.md](THIRD_PARTY.md).
