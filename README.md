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
- Merging. When two mods change the same archive (.sarc, .pack, .szs, .zs and the other SARC types) or the same .ips patch, ModGrab keeps both and builds one file that has both mods' changes in it, instead of the newer one wiping out the older. Archives are merged file by file inside, against the game's own copy, and nested archives get merged too. If both mods change the same file inside, the newer mod's version wins and ModGrab tells you which one clashed.
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
- `layers/<mod id>/` holds each mod's own copy of files that got merged, and `originals/<title id>/` holds the game's untouched copies ModGrab merges against.

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

Bump `APP_VERSION` in the Makefile and push. Then publish a release on GitHub whose tag matches it (like `v1.2.0`), without attaching anything.

The `release` workflow in `.github/workflows/release.yml` builds `ModGrab.nro` with devkitPro's Docker image and attaches it to the release. It refuses to run if the tag and `APP_VERSION` don't match. To rebuild an existing release, run the workflow by hand from the Actions tab and give it the tag. GitHub records a SHA-256 for every release file, and ModGrab's self-updater (pointed at the repo by `cfg::UpdateRepo` in `include/config.hpp`) refuses any download that doesn't match it.

To regenerate `icon.jpg`:

```
pip install -r requirements.txt
python tools/make_icon.py icon.jpg
```

## Troubleshooting

If it says it couldn't reach the server, the network is blocking the Switch or needs a proxy. ModGrab uses the proxy set in the Switch's wifi settings (System Settings > Internet > your network > Change Settings > Proxy). School and work networks often block homebrew traffic even when the site opens fine on a phone.

If it says the Switch's DNS is blocking the site, something on the Switch (atmosphere/hosts or a custom DNS) is sending gamebanana.com to 127.0.0.1.

Certificate errors almost always mean the Switch's date is wrong, so check that first. Past that, the Switch only trusts a short list of certificate authorities. ModGrab adds the common ones from `romfs/certs.pem` (made with `python tools/make_certs.py`) and still checks every certificate. If a site moves to an authority that isn't in the list, add its root to `ROOTS` in the script and rebuild.

## Known limits

- GameBanana's MD5 checksums aren't checked (libnx has no MD5), only the file size.
- Mods packed in a layout without `romfs`, `exefs`, `exefs_patches`, `contents` or `atmosphere` folders can't be placed automatically.
- Update checks compare the mod's version string with what you installed and look for files uploaded after you installed.
- The game version check depends on the mod author writing the version somewhere. "No game version listed" means ModGrab couldn't tell, not that the mod is fine.
- exefs_patches (.ips) only load on the exact build they were made for. ModGrab can't read a game's build ID, so it can't verify those.
- There's no sort or category filter on the mod list. GameBanana's API ignored the sort and filter parameters in testing, so they were left out rather than shipped broken.
- Merging archives needs the game's original copy of the file, and a homebrew app can only read that while it's running as the game. Open ModGrab by holding R while you start the game once (title override), and it merges anything that's waiting, then keeps the originals so it doesn't need that again. Until then the newest mod's copy is used. .ips patches merge without this.
- When the game updates, merged archives get rebuilt against the new original the next time you open ModGrab through the game. Mods made for the old version still carry the old version's files inside their archives, so merging can't undo that.
- Merging only works on SARC archives and .ips patches. Two mods that replace the same .bfres, .bntx, .byml or any other file still overlap, and the newer one wins like before.
- Mods installed with an older ModGrab (before backups existed) don't have backups, so uninstalling them only deletes their own files.

## Credits

See [THIRD_PARTY.md](THIRD_PARTY.md).
