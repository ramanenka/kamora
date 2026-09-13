# Kamora Backup

A Kirigami app that keeps a [borg](https://borgbackup.org) repository on a USB
drive up to date.

It starts with a single **Add backup configuration** button. You browse to the
repository folder on the drive itself, add the folders to back up and the
patterns to leave out, and say how often a backup should happen. After that
Kamora stays out of the way: it watches for that drive by the UUID of its
filesystem, mounts it when it shows up, and runs the backup if one is due.

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
* **Tray icon** — a `KStatusNotifierItem` that is `Passive` while everything is
  up to date and `Active` when a backup is due or running (`NeedsAttention` when
  the last one failed). Plasma's default *Show when relevant* therefore keeps it
  hidden until a backup is actually due, and hides it again once one has been
  taken. Set the item to *Always show* in Plasma's tray settings if you prefer.
* **Passphrase** — for an encrypted repository the passphrase is stored in
  KWallet, with a 0600 file next to the configuration as a fallback.
* **Autostart** — enabled from the setup page; it writes
  `~/.config/autostart/org.kamora.Backup.desktop` with `--background`, so
  Kamora starts into the tray at login.

## Building

Build dependencies (Fedora):

```
sudo dnf install gcc-c++ cmake ninja-build extra-cmake-modules \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qttools-devel \
    kf6-kirigami-devel kf6-kirigami-addons-devel kf6-kcoreaddons-devel \
    kf6-ki18n-devel kf6-kconfig-devel kf6-kiconthemes-devel \
    kf6-kstatusnotifieritem-devel kf6-knotifications-devel \
    kf6-kdbusaddons-devel kf6-solid-devel kf6-kwallet-devel \
    borgbackup
```

Then:

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
cmake --install build
```

That puts `kamora` in `~/.local/bin`, the desktop entry in
`~/.local/share/applications` and the notification setup in
`~/.local/share/knotifications6` - no root needed. Use the default prefix with
`sudo cmake --install build` for a system-wide install instead.

The app also runs straight from the build directory (`./build/bin/kamora`), but
notifications stay unattributed until the desktop entry and notifyrc are
installed.

## Command line

| Option | Effect |
| --- | --- |
| `--background` | start into the tray without opening the window |
| `--backup-now` | start a backup immediately |
| `--configure` | open the window on the configuration page |

All three also work while an instance is already running: the second launch
hands its arguments over instead of starting a second copy, which is what the
*Back up now* and *Configure backup* entries in the launcher's context menu
use.

## Layout

| File | Contents |
| --- | --- |
| `src/backupconfig.*` | configuration and last-run state, stored in `kamorarc` |
| `src/drivemonitor.*` | Solid-based detection, path resolution, mounting and unmounting |
| `src/borgrunner.*` | the borg process queue and `--log-json` parsing |
| `src/backupcontroller.*` | scheduling, orchestration, the QML singleton `Kamora` |
| `src/trayicon.*` | the status notifier item and its relevance states |
| `src/qml/` | the Kirigami interface |
| `data/` | desktop entry and notification definitions |

## Notes

* borg exit code 1 means warnings (a file changed while it was read, say); the
  archive is kept and the run counts as finished. Anything above that fails the
  run, and borg's Python traceback is condensed to its last line for the UI -
  the full text stays in the log.
* An automatic run is never started twice in a row, and a failed one waits
  half an hour before it is retried on its own. *Back up now* ignores both.
* `borg prune` and `borg compact` only run when at least one retention value
  is non-zero.
* A folder that resolves to a fixed disk rather than a removable drive is
  accepted, with a warning - the drive then simply always counts as connected.
