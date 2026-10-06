# Kamora Backup

[![Copr build status](https://copr.fedorainfracloud.org/coprs/ramanenka/kamora/package/kamora/status_image/last_build.png)](https://copr.fedorainfracloud.org/coprs/ramanenka/kamora/package/kamora/)

A Kirigami app that keeps a [borg](https://borgbackup.org) repository on a USB
drive up to date.

It starts with a single **Add backup configuration** button. You browse to the
repository folder on the drive itself, add the folders to back up and the
patterns to leave out, and say how often a backup should happen. After that
Kamora stays out of the way: it watches for that drive by the UUID of its
filesystem, mounts it when it shows up, and runs the backup if one is due.

Add as many configurations as you have drives. Each one has its own drive,
repository, schedule, retention and log, and the overview page lists them all
with what each is waiting for. Settings that are about Kamora itself rather
than about one backup - whether it starts at login - live under **Settings**.

## How it behaves

* **Choosing the repository** — you pick a folder in an ordinary folder dialog
  and Kamora splits it into the UUID of the volume it sits on and the path
  below that volume's mount point. Only that pair is stored, never the absolute
  path, so `/run/media/you/BACKUP/borg` today still resolves correctly when the
  same drive turns up as `/media/BACKUP1` tomorrow. The drive has to be
  connected while you choose, because an unmounted filesystem has no path to
  browse; the setup page offers to mount it for you.
* **Drive detection** — Solid reports volumes as they are attached, and the
  backup drive is recognised by that stored UUID, whatever device node it gets.
  Mounting goes through Solid/UDisks2, the same path Dolphin uses, so no root
  privileges are involved.
* **Backups** — `borg create` with the configured compression and exclusions,
  then `borg prune` and `borg compact` for the retention you asked for. Progress
  comes from borg's `--log-json` stream. The repository is created on first use.
* **One at a time** — while a backup is running no other one can start, so two
  never compete for the same disk, or for the same borg lock when they share a
  drive. Every other configuration's *Back up now* rests until it is done; one
  that was due meanwhile starts on its own shortly after.
* **Tray icon** — a `KStatusNotifierItem` that is `Passive` while everything is
  up to date and `Active` when a backup is due or running (`NeedsAttention` when
  the last one failed). It reflects all configurations at once: the most
  demanding state wins. Plasma's default *Show when relevant* therefore keeps
  it hidden until a backup is actually due, and hides it again once one has
  been taken. Set the item to *Always show* in Plasma's tray settings if you
  prefer.
* **No repository encryption** — repositories are always created with
  `--encryption none`, so Kamora never holds a passphrase and backups need
  nobody present. Put the repository on a LUKS drive if you want the archives
  encrypted at rest; Kamora unlocks such a drive through Solid, and the system
  asks for that passphrase itself.
* **Autostart** — enabled under *Settings*, where it applies to Kamora as a
  whole rather than to one backup. It writes
  `~/.config/autostart/io.github.ramanenka.kamora.desktop`, with `--background`
  unless you ask for the window, so Kamora starts into the tray at login. The
  settings page names the file the running build writes, which is not the same
  one for a development build.
* **Configuration file** — `~/.config/kamorarc` keeps the application settings
  in `[General]` and one `[Backups][<id>]` group per configuration, with that
  configuration's last run in `[Backups][<id>][State]`. A development build
  uses `~/.config/kamoradevrc` instead.

## Building

Build dependencies (Fedora):

```
sudo dnf install gcc-c++ cmake ninja-build extra-cmake-modules \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qttools-devel \
    kf6-kirigami-devel kf6-kirigami-addons-devel kf6-kcoreaddons-devel \
    kf6-ki18n-devel kf6-kconfig-devel kf6-kiconthemes-devel \
    kf6-kstatusnotifieritem-devel kf6-knotifications-devel \
    kf6-kdbusaddons-devel kf6-solid-devel \
    borgbackup
```

There are two flavours. The release one is what gets packaged and installed
system-wide. The development one is the same application under its own names,
so it can be built, installed and run without disturbing a release build that
is already on the machine - and without root, since it lives in `~/.local`.

### Development

```
cmake -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKAMORA_DEV=ON
cmake --build build-dev
cmake --install build-dev
```

`~/.local` is the default prefix for this flavour, so nothing here asks for
root. It installs as `kamoradev` and shows up in the launcher and the
notification settings as *Kamora DEV*, with a violet drive for its application
icon and a wedge in the corner of its tray icons. Everything inside the window
reads the same in both, the About box included.

The app also runs straight from the build directory
(`./build-dev/bin/kamoradev`), but notifications stay unattributed until the
desktop entry and notifyrc are installed.

### Release

Releases are published to the
[ramanenka/kamora](https://copr.fedorainfracloud.org/coprs/ramanenka/kamora/)
COPR repository:

```
sudo dnf copr enable ramanenka/kamora
sudo dnf install kamora
```

after which `dnf upgrade` brings each new release.

To cut one, run `packaging/release.sh` on a clean, pushed `main`. It takes the
latest `vX.Y` tag, tags HEAD with the next minor version (`v0.1` when there is
none yet) and pushes the tag. GitHub tells COPR through a webhook, and COPR
runs `.copr/Makefile` on the tagged checkout to build the source package and
then the RPMs. The version comes from the tag alone: the spec carries a
placeholder, and the changelog entry lists the commits since the previous tag.

To build the package locally instead:

```
sudo dnf install rpm-build
packaging/build-rpm.sh
sudo dnf install ~/rpmbuild/RPMS/x86_64/kamora-*.rpm
```

`packaging/build-rpm.sh` packages what is committed rather than the working
tree, since it takes its tarball from `git archive`. On a release tag the
package gets that version; anywhere else a snapshot one such as `0.1^3.gabc1234`,
which sorts after `0.1` and before `0.2`. To install a release build without
the detour through a package:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### What the two do not share

| | release | development |
| --- | --- | --- |
| executable | `kamora` | `kamoradev` |
| default prefix | `/usr` | `~/.local` |
| launcher and notifications | Kamora | Kamora DEV |
| desktop entry, D-Bus name, icons | `io.github.ramanenka.kamora` | `io.github.ramanenka.kamoradev` |
| settings | `~/.config/kamorarc` | `~/.config/kamoradevrc` |
| notification setup | `kamora.notifyrc` | `kamoradev.notifyrc` |

Every one of those names comes from the `KAMORA_DEV` block at the top of
`CMakeLists.txt`, which fills in `src/kamoraconfig.h.in` for the C++ and the
`.in` templates in `data/` for the desktop entry and the notification setup.
The dev flavour's icons are drawn from the release ones by
`cmake/KamoraDevIcons.cmake`, so there is only ever one set kept by hand.

Carry an existing configuration over to a development build with
`cp ~/.config/kamorarc ~/.config/kamoradevrc`; the two never read each
other's.

## Command line

| Option | Effect |
| --- | --- |
| `--background` | start into the tray without opening the window |

Launching Kamora while an instance is already running does not start a second
copy: the running one brings its window back up instead.

Everything else is done from the window - *Add configuration* for a new backup,
*Configure…* on a backup's card for an existing one, and a card's *Back up now*
to run one.

## Layout

| File | Contents |
| --- | --- |
| `src/appsettings.*` | settings that belong to Kamora rather than to one plan |
| `src/backupconfig.*` | one plan's settings and last-run state, stored in `kamorarc` |
| `src/drivemonitor.*` | Solid-based detection, path resolution, mounting and unmounting |
| `src/borgrunner.*` | the borg process queue and `--log-json` parsing |
| `src/backupplan.*` | one plan at work: due-ness, drive, borg run, log |
| `src/backupcontroller.*` | the plans, which one runs, the QML singleton `Kamora` |
| `src/trayicon.*` | the status notifier item and its relevance states |
| `src/qml/` | the Kirigami interface |
| `data/` | desktop entry, application icons and notification definitions |
| `cmake/` | the dev flavour's icons, drawn from the release ones |
| `packaging/` | the RPM spec for the release flavour, a script to build it and one to cut a release |
| `.copr/` | the hook COPR calls to build the source package from a release tag |

## Notes

* borg exit code 1 means warnings (a file changed while it was read, say); the
  archive is kept and the run counts as finished. Anything above that fails the
  run, and borg's Python traceback is condensed to its last line for the UI -
  the full text stays in the log.
* An automatic run is never started twice in a row, and a failed one waits
  half an hour before it is retried on its own. *Back up now* ignores both.
* A configuration can be switched off in its settings. It is then left out of
  automatic backups but can still be run by hand.
* `borg prune` and `borg compact` only run when at least one retention value
  is non-zero.
* A folder that resolves to a fixed disk rather than a removable drive is
  accepted, with a warning - the drive then simply always counts as connected.
