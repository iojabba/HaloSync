# Halo Sync

Halo Sync is a music library manager, player and drag-and-drop USB sync tool. It runs on Windows, macOS and Linux. It is an independent clean-room project built from the behavioral specification in [`docs/SPEC.md`](docs/SPEC.md). It contains no Microsoft code or assets.

> **Zune players are not supported.** A Zune only allows file access after Microsoft's MTPZ authentication, and this project deliberately doesn't implement it (SPEC §4.1). When a Zune is plugged in, Halo Sync recognizes it and explains this. It syncs to USB drives, SD cards, and other players that show up as a drive or folder.

## What's in this version (Phase 2a MVP)

| Area | Features |
|---|---|
| **Library** | Monitored folders with live watching and incremental rescans. A SQLite library. Tags read with TagLib: MP3, AAC/M4A, FLAC, Ogg, Opus, WMA, WAV, AIFF, APE and WavPack. Duplicate, missing-art and missing-info reports. Remove from collection, with optional file deletion. |
| **Views** | Artists, albums (with cover thumbnails), genres, songs (choose columns by right-clicking the header), playlists, now playing and reports. Search across all of them. |
| **Editing** | Edit info for one song or many; tags are written into the files. Merge an artist, album or genre by dragging it onto another (or right-click › Merge into). Paste album art from the clipboard. **Find album info** with MusicBrainz and Cover Art Archive. |
| **Playlists** | Standard playlists: drag songs onto them, reorder, remove duplicates. Autoplaylists with the full rule set from SPEC §2.5: album artist, artist, composer, genre, album, rating, year range, recently added and play count, each with include or exclude, plus arrange order and a limit by song count or minutes. Import and export M3U/M3U8, PLS, XSPF and ZPL/WPL. |
| **Playback** | Now Playing queue, shuffle, repeat (all or one), Like/Don't like ratings, play counts and last-played dates, ReplayGain, a mini player, keyboard shortcuts and media keys. On Linux it also uses MPRIS desktop media controls. |
| **Quickplay** | Pinned albums, artists, genres and playlists as tiles, plus recently played songs. |
| **Sync** | Devices are any drive or folder. Three modes: **All**, **Items I choose** (sync groups), and **Manual** (copies what you send and never removes anything). Formats the device can't play are converted with ffmpeg to MP3, AAC, Opus, WMA or FLAC, with an optional bit-rate cap. Converted files are cached. Disliked songs can be skipped. Playlists are written to the device as relative `.m3u` files. Sync warns before overfilling, shows storage use by category, and can be cancelled. Halo Sync only ever deletes files it put on the device itself; it tracks them in `.halosync/manifest.json`. |
| **Look** | An original Metro-inspired design: big lowercase pivots, flat surfaces, dark or light theme and accent colours. |
| **Other** | Portable mode (put an empty file named `portable` next to the program). A log file. Command-line jobs for scripting. |

The SPEC also lists features that aren't in this version yet: plain MTP devices, smart fill, podcasts, audiobooks, lyrics, scrobbling, Discord presence, Smart DJ, Mixview, CD rip and burn, and UPnP sharing (Phase 2b/2c).

Known limits:
- Qt Multimedia has no gapless mode, so there is a short pause between songs.
- Windows SMTC and macOS Now Playing integration aren't done yet. Media keys work while the window has focus.

## Building

You need CMake 3.21 or later, a C++20 compiler, Qt 6.4 or later (Widgets, SQL with SQLite, Multimedia, Network, Concurrent; DBus on Linux) and TagLib 1.11 or later. ffmpeg is needed at runtime for conversion, and by the tests.

### Linux (Ubuntu/Debian)
```sh
sudo apt install qt6-base-dev qt6-multimedia-dev libqt6sql6-sqlite libtag1-dev ffmpeg cmake ninja-build \
                 gstreamer1.0-plugins-good gstreamer1.0-libav
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
(cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure)
./build/halo-sync
```
Playback on Linux uses GStreamer, so install the plugins above.

### Windows 10/11
1. Install Visual Studio 2022 (Desktop C++), the [Qt online installer](https://www.qt.io/download-qt-installer) (Qt 6.x MSVC 64-bit plus the *Qt Multimedia* add-on) and [vcpkg](https://github.com/microsoft/vcpkg).
2. `vcpkg install taglib:x64-windows`
3. In a "x64 Native Tools" prompt:
   ```bat
   cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2019_64 -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake -DHALO_BUILD_TESTS=OFF
   cmake --build build --config Release
   C:\Qt\6.7.3\msvc2019_64\bin\windeployqt build\Release\halo-sync.exe
   ```
4. Copy `tag.dll` (and its `zlib` DLL) from `vcpkg\installed\x64-windows\bin` next to the exe. Also put `ffmpeg.exe` there, or set its path in Settings.

`.github/workflows/build.yml` does all of this on GitHub Actions for Windows, macOS and Linux. Each push to `main` publishes the Windows zip (with ffmpeg included) and the Linux binary as the "latest" pre-release under Releases.

### macOS
```sh
brew install qt taglib pkg-config ninja ffmpeg
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" && cmake --build build
```

## Command line
```
halo-sync --add-folder ~/Music          # add a monitored folder and scan it
halo-sync --scan --list                 # rescan, then print the collection
halo-sync --sync-to /media/PLAYER --sync-mode all
halo-sync --data-dir ./test-data ...    # use a separate library
```

## Keyboard
| Key | Action |
|---|---|
| Ctrl+P | play/pause |
| Ctrl+F / Ctrl+B | next / previous |
| Ctrl+Shift+F / Ctrl+Shift+B | skip ahead / back 10 s |
| Ctrl+H | shuffle |
| Ctrl+T | repeat |
| Ctrl+L | like |
| Ctrl+E | search |
| Ctrl+M | mini player |
| F7 / F8 / F9 | mute / volume down / volume up |
| Alt+1…4 | switch pages |

## Layout
```
src/core   library, database, tags, playlists, autoplaylist rules, sync engine, transcoder, MusicBrainz (no UI)
src/app    Qt Widgets user interface
tests      Qt Test unit tests (they generate real audio with ffmpeg)
```
