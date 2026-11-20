# SPEC: Zune-style Media Library & Sync Utility (Clean-Room Phase 1)

Status: **Draft v2 for approval** (revised after Eric's wishlist; Phase 2 must not start until Eric approves)
Date: 2026-11-18

---

## 0. Scope, method and exclusions

### 0.1 Subject
- Input: `ZunePackage.exe` (283,639,032 bytes, SHA-256 `ef5e5debd76ca7df2f7b6bf7095e1354ee4f9bafdbbd29c96cb1ca87695fddee`).
- It is a Microsoft SFXCAB self-extractor wrapping a cabinet. Inside: `x86/packages/zune-x86.msi` (main product, ~50 MB), `zunewmdu-x86.msi` (device update helper), 21 language MSIs, Windows XP prerequisite hotfixes, `startzunelocal.exe`, `autorun.inf`.
- Product: Zune software 4.8 (2011 build).

### 0.2 Method (static, read-only)
- The installer was **never executed**. It was unpacked with `7z` and `msiextract` into its own empty scratch folder on an isolated Linux container.
- Evidence came only from: bundled help pages (`en-US/*.htm`), config files, UPnP service description XMLs, PE version metadata, resource directory listings, the English string table (`en-US/ZuneResources.dll.mui`) and identifier names in the compiled UI resource table (`ZuneShellResources.dll` → `_DATATABLE.UIB`).
- **No program code was disassembled or decompiled.** No code, markup or images are copied into this spec. Short UI labels are quoted only to pin down behavior. This keeps Phase 2 a clean implementation from a behavioral description.
- Evidence tags used below: `[HELP:file]` = bundled help page, `[STR:id]` = English string-table entry, `[UIID:name]` = UI resource identifier, `[FILE:path]` = installed file, `[XML:file]` = service description.

### 0.3 Explicit exclusions (not analyzed, not documented, not to be built)
These were seen only as file names or category labels and were skipped:
- Licensing, DRM, activation, product keys, EULA dialogs.
- Device authentication (`ZuneMTPZ.dll`, the MTPZ handshake), `Network Sharing/Handshake.xml`, `MediaReceiverRegistrar.xml` (device authorization/validation).
- Account sign-in (`msidcrl40.dll`, Windows Live ID), Marketplace, Zune Music Pass, purchases, rentals, billing, payment forms.
- Social features (friends, inbox, Zune Tag, messaging), telemetry/feedback program, device firmware update/restore/rollback, games install.
- The "2 computers or devices every 30 days" device-swap rule `[STR:36167]` (an account/licensing limit).
- Smart DJ's online recommendation service (it needs Microsoft servers). A local-only substitute is listed as optional in §3.

---

## 1. Inventory (observed)

### 1.1 Installed components `[FILE]` with their PE "FileDescription"
| Area | Files | Description string |
|---|---|---|
| Main UI | `Zune.exe`, `ZuneShell.dll`, `ZuneShellResources.dll`, `UIX*.dll` | "Microsoft Zune", "ZuneShell", "Microsoft UIX Framework / Rendering Engine" |
| Library DB | `ZuneDB.dll`, `ZuneSE.dll`, `ZuneQP.dll`, `ZuneDBApi.dll` | "OLEDB Provider", "Storage Engine", "Query Processor", "Zune DB API" |
| Metadata | `ZuneMde.dll` | "ZuneMDE" (inferred: metadata extraction) |
| Encode/rip | `ZuneEnc.exe`, `ZuneEncEng.dll`, `l3codecp.acm` | "Encoder Helper", "Encoding Module", MP3 codec |
| Decode/playback | `ZuneAACDec.dll`, `ZuneH264Dec.dll`, `ZUNEMP4SDECD.dll`, `ZuneEvr.dll`, `ZunePresenter.dll`, `ZuneDXVA2.dll`, `ZuneEffects.dll`, `ZuneMBR.dll`, `ZuneSrcWrp.dll` | AAC, H.264, MPEG-4 video decoders, video renderer, DXVA, effects |
| Device sync | `ZuneSP.dll`, `ZuneSH.dll`, `ZuneZMDB.*.dll`, `ZuneService.dll`, `Drivers/Zune/*` | "WMDM Service Provider", "Device Communication Support", per-device-family media DB libraries |
| Wireless sync | `ZuneWlanCfgSvc.exe`, `WMZuneComm.exe`, `ZuneNetProxy`, `ZuneTcp2Udp` | Wireless config, Windows Mobile comm |
| Network sharing | `ZuneNss.exe`, `ZuneNssci.dll`, `Network Sharing/ContentDirectory.xml` | "Network Sharing Service" (UPnP ContentDirectory) |
| Shell | `ZuneShellExt.dll`, `ZuneTaskbar.dll`, `ZuneLauncher.exe`, `ZuneShare.exe` | Explorer extension, taskbar integration, auto-launch on device connect |
| Config | `ZuneConfig.exe`, `ZuneCfg.dll`, `Zune.exe.config` | Configuration |

### 1.2 Runtime notes
- `Zune.exe.config` targets .NET CLR v2.0.50727 and defines three trace switches: `Collection`, `Shell`, `DataProvider` (all Off). So the shell is partly managed code over native DB, codec and device layers.
- The UI is built from ~242 compiled UI modules `[FILE:ZuneShellResources.dll RCDATA *.UIX]`. Module names map to screens, e.g. `CDVIEW`, `AUTOPLAYLISTDIALOG`, `EDITMEDIAINFODIALOG`, `FINDALBUMINFODIALOG`, `MANAGEMENTRIP`, `MANAGEMENTCOLLECTION`, `DEVICESYNCGROUPS`, `QUICKPLAY`, `PHOTOSLIDESHOW`, `MINIMODE`, `NOWPLAYINGLAND`.
- Four UI sound resources: rip/reverse-sync complete, sync/burn complete, download, inbox.

---

## 2. Observed behavior

### 2.1 Top-level navigation
- Sections: **Quickplay**, **Collection**, plus account/online sections (Marketplace, Social), which are excluded. `[HELP:quickplay]`, `[STR:36697-36699]`
- Collection pivots: **Music, Videos, Pictures, Podcasts**, plus Channels, Audiobooks and "Content from other computers" `[STR:36648-36652, 36615-36617]`.
- The startup view is configurable under Settings › General › Startup View `[HELP:quickplay]`.
- Persistent bottom-left icons act as drop targets: the **playlist icon**, the **disc (burn) icon** and the **device icon** `[HELP:playlists, syncing]`, `[STR:46003]`.
- Mini mode: a compact player window for audio and video `[UIID:MINIMODE, MINIMODEAUDIO, MINIMODEVIDEO]`.
- Now Playing: a full-screen view, optionally shown automatically when idle `[STR:36600]`.

### 2.2 Collection model
- The collection **links to files** in folders. It does not own or move them `[HELP:zunefaq]`.
- **Monitored folders** per media type (Music, Videos, Pictures, Podcasts). The user can add or remove folders. New files in them are added automatically `[STR:36099, 36111, 36121, 36034-36035]`.
- Removing a monitored folder removes its items from the collection and deletes no files `[STR:36520-36521]`.
- An invalid or restricted path gives a "CAN'T ADD" error `[STR:36379-36380]`.
- **Default save folders** for rips and podcasts are separate from the monitored folders, and are themselves monitored `[STR:36178, 36365, 36512]`.
- Options `[STR:36045-36050]`:
  - Restore media previously removed from the collection (this is not undelete).
  - Import files, playlists and ratings from another media player.
  - Delete files from disk when they are deleted from the collection (toggle).
  - Add media files to the collection when they are played.
- Playing a file outside the monitored folders prompts "Add to collection" `[STR:44024-44025]`.
- Deleting asks for confirmation and says whether files will be removed from the computer only or also from the device `[STR:44061-44071]`.
- **Duplicate detection**: duplicate songs are flagged, with "Show duplicates" and a setting to show duplicate entries in the Songs view `[STR:36591-36595, 36663]`.
- **Ratings** use three states: Like, Don't like, Unrated (the UI calls these "LikeIt/HateIt/NotRated"). They can be common to all users or personal per user `[STR:46063-46065, 36607-36610]`, `[UIID:NPRatingLikeIt…]`.
- Supported formats `[HELP:zunefaq]`, `[STR:701-709, 35135-35144]`:
  - Audio: .wma, .mp3, .m4a, .m4b, .mp4 (also QuickTime and WAV audio are listed as types).
  - Video: .wmv, .m4v, .mp4 (also QuickTime video).
  - Pictures: .jpg/.jpeg/.jpe/.jfif.
  - Playlists: **.zpl** (Zune playlist) and **.m3u**.
- File-type associations: a "File Types" settings page with per-type checkboxes and Select All `[STR:36002, 36105, 36108]`.

### 2.3 Music views `[HELP:metadata]`
- **Artists**: album artists listed A–Z on the left. Selecting one shows that artist's albums and songs.
- **Genres**: albums grouped by genre.
- **Albums**: A–Z by default. It can be sorted by release year, artist or date added, and the song list is shown on the right.
- **Songs**: a detail grid with user-selectable columns (right-click the header).
- **Playlists**: see §2.5.
- Song columns available `[STR:35024-35056]`: Title/Song, Album, Artist, Contributing artist, Genre, Subgenre, Length, Date added, Date last played, Play count, Size, Track number, Release year, Composer, Rating, Publisher, Conductor, Status, Bitrate `[UIID:TrackBitrateCell]`.
- Placeholders: "Unknown Genre" and "Unknown Date" `[STR:35057-35058]`.
- Hovering an album gives a Play action `[HELP:quickplay]`.

### 2.4 Metadata editing `[HELP:metadata]`
- Music fields: song title, album name, album artist, contributing artist, genre, release year, rating, length. Track and disc numbers and composer also appear `[STR:43108-43125]`.
- **Edit**: right-click a song or album and choose Edit to open an inline/multi-field editor `[UIID:EDITMEDIAINFODIALOG]`.
- An album name can't be blank or contain certain reserved characters `[STR:44040]`. Paths reject `< > : " / | * ?` `[STR:2046]`.
- **Merge**: drag an artist, album or genre onto another to rename all items to the target.
- **Find album info (online)**: search by artist or album, pick a result, match songs, then confirm the changes `[UIID:FINDALBUMINFODIALOG, FINDALBUMINFOSONGMATCH, CONFIRMALBUMCHANGESDIALOG]`.
- Auto-update setting: "Only add missing information" or "Replace all media information" `[STR:36160-36161, 36047]`.
- **Album art**: shown when available. "Paste album art" works from the clipboard `[HELP:rippingcds]`.
- Video metadata: content type, title, release year, genre, description. Video categories are All, TV, Music, Movies and Other. Users can recategorize (for example Other → Movies).

### 2.5 Playlists `[HELP:playlists]`, `[STR:46000-46137]`
- **Standard playlist**:
  - New Playlist, then a name. Duplicate names are rejected and invalid names are rejected `[STR:46022, 46024]`.
  - Add songs by dragging them to the playlist icon. Reorder by dragging. Remove from playlist.
  - Rename. Sort options are Oldest–Newest and Newest–Oldest. "Remove duplicates" `[STR:46090]`.
  - The summary shows "N songs, M minutes" `[STR:46077, 46121-46123]`.
- **Autoplaylist** (rule-based, dynamic). Fields seen in the dialog `[STR:46041-46110]`, `[UIID:*Criterion]`:
  - Name.
  - Music filters: Album artist, Contributing artist, Composer, Genre, Album, Rating, Year (range "x to y"), Recently added "in the last N days", Song plays (All, Never played, Less than N, More than N).
  - Rating values: All, Like, Don't like, Unrated, Like or unrated.
  - Each filter can **Include** or **Exclude**. A semicolon separates multiple values ("Pop;Rock") `[HELP:playlists]`.
  - Arrange: Randomly, by album artist, song artist, composer, album, genre, date added, year, song title, most played, least played.
  - Limit: No limit, N songs, or N minutes.
  - Empty result message: "None of the songs in your collection fits the rules".
  - Autoplaylist contents can only change by editing the rules.
- **Now Playing** queue: remove an item, "Save as playlist" with a Replace prompt on name collision `[STR:46084-46087]`.
- Playback skips missing or deleted songs. Read-only playlist files can't be changed or deleted `[HELP:troubleshooting]`.
- Default playlist folder: My Music › Zune › Playlists `[HELP:troubleshooting]`.

### 2.6 Quickplay `[HELP:quickplay]`
- Pin to Quickplay works for an album, artist, playlist, genre, video or podcast series.
- The Quickplay page shows pinned items and recently played items, each playable with one click.

### 2.7 Playback
- Transport controls: play/pause, stop, previous/next (back/forward), rewind/fast-forward, volume up/down, mute, shuffle toggle, repeat toggle, and a search-focus key `[UIID:KeyHandler*]`.
- A position/duration display `[UIID:PositionDurationFormatText]`.
- Settings `[STR:36051, 36055, 36672-36678]`: allow screen savers during playback, graphics and video enhancement, and a screen graphics tier (Basic to Premium).
- Optional sounds `[STR:36437-36438]`. Background themes `[STR:36398-36406]` (cosmetic).

### 2.8 Pictures & slideshow `[HELP:pictures]`, `[STR:44000-44011, 36301]`
- Picture albums come from folders and can be reorganized by drag and drop. Sort by date taken (default) or by name.
- Clicking a thumbnail opens a full view. The slideshow supports play, pause, resume, previous and next, with speed set as "change slide every N seconds".
- Album names can't be blank and have character restrictions.

### 2.9 Podcasts `[STR:36087-36192, 36329, 36347, 36436]`
- Subscribe by entering a feed URL.
- Global defaults apply to new subscriptions, with per-series overrides ("Series Settings"):
  - Keep: 1, 5 or 15 episodes, or All.
  - Playback order: newest first or oldest first.
- Device sync per series: don't sync, sync all downloaded, sync all unplayed and partially played, or sync only the first unplayed episode.

### 2.10 CD ripping `[HELP:rippingcds]`, `[STR:33000-33069, 36081-36084, 36162-36163, 204-210]`
- Insert a CD, select the disc view, check or uncheck tracks, then **Start Rip** or **Cancel Rip**.
- Per-track status: Pending, %, In collection, Not added, Incomplete, Error. Overall status: "Song N of M", complete, cancelled, failed.
- Multiple drives are supported, listed as "Additional CDs".
- Output goes to the configured rip folder.
- Formats and quality:
  - **WMA (constant bit rate)**: 48, 64, 96, 128, 160, 192 Kbps.
  - **WMA (variable bit rate)**: five quality tiers, about 40–75 up to 240–355 Kbps.
  - **WMA Lossless**: about 470–940 Kbps.
  - **MP3**: 128, 192, 256, 320 Kbps.
  - The UI shows an estimated MB per CD for each option.
- Options: rip a CD automatically when inserted, eject after ripping.
- Track names come from online lookup. Offline rips get placeholder names ("Track 1"…) and are filled in later when online.

### 2.11 Burning `[STR:33016-33066, 36043-36044, 36064-36070, 36116-36119, 46023-46034]`
- A **burn list** works like a special playlist: drag items to the disc icon. Actions are add, remove, clear, and "Create new" or "Add to existing".
- Disc format: **Audio CD** or **Data CD/DVD**.
- Burn speed: Fastest, Fast, Medium, Slow.
- Audio CD options: apply volume leveling, eject after burning.
- Data disc option: keep the original bit rate (recommended) or convert to 32–192 Kbps to fit more music.
- The UI shows minutes used and available, and size used and available.
- States: pending, preparing, "Song N of M", finalizing, done, cancelled, failed, erase disc. Errors include "insert blank disc", "can't burn audio to DVD" and "connect burner".

### 2.12 Device sync `[HELP:syncing, pictures, playlists]`, `[STR:36144-36689]`
- Device relationship: **linked** (a full sync partner) or **guest** (manual only). Name the device. "Forget this player". "Erase all content". View a device summary with capacity and a usage gauge.
- Sync modes per media type (Music, Videos, Pictures, Podcasts): **All**, **Items I choose**, or **Manual**.
  - **All / Items I choose (automatic)**: library changes (adds, deletes, metadata fixes) propagate to the device. Ratings sync both ways.
  - **Items I choose** creates a **sync group** each time an artist, album, playlist or folder is dragged to the device. Sync groups can be listed, edited and removed (removing one deletes its content from the device after a confirm). Complex rule groups are also possible `[UIID:COMPLEXSYNCRULEDIALOG]`.
  - **Manual**: drag items to the device to sync now. Collection deletions don't touch the device.
- "Don't sync songs rated [Don't like]" `[STR:36441]`.
- If no device is connected, dragged items are **queued** for the most recently connected device, or a chosen known device.
- Interrupted syncs resume on the next connection.
- **Reverse sync**: on phones, new pictures and videos copy device → PC, with optional keep-on-device.
- Collection items show a small device icon when they are on the device. The device view lists device content per media type and allows manual removal.
- Overfill handling: "uncheck items until you're no longer overfilled" `[STR:36497]`. Space reservation `[UIID:DEVICESPACERESERVATION]`.
- **Transcoding** during sync:
  - Audio above a "standard quality" threshold bit rate is converted to a "device quality" target bit rate.
  - Video can be optimized for the device or for TV output. There is an option to convert only unsupported files.
  - Conversion cache folder (change or clear), a reserved-space cap in MB, and background conversion `[STR:36259-36268, 36442-36447, 36492-36494]`, `[UIID:AudioThresholdBitRate, AudioTargetBitRate]`.
- **Auto-launch** on device connect (toggle) `[STR:36256]`.
- Wireless sync: SSID, network type (Open, WEP, WPA/WPA2 with AES or TKIP), key, and scanning. Syncs after 10 minutes on AC power within range `[STR:36223-36392, 36835]`.

### 2.13 Network sharing (UPnP/DLNA server) `[XML:ContentDirectory.xml]`
- Exposes a standard UPnP **ContentDirectory:1** service with these actions: `GetSearchCapabilities`, `GetSortCapabilities`, `GetSystemUpdateID`, `Browse` (ObjectID, BrowseFlag, Filter, StartingIndex, RequestedCount, SortCriteria → Result, NumberReturned, TotalMatches, UpdateID), `Search`.
- It also has `ContainerUpdateIDs` and `SystemUpdateID` state variables.
- The authorization services (`MediaReceiverRegistrar`, `Handshake`) are **excluded** (§0.3).

### 2.14 Settings map (observed page names) `[STR:36000-36010, 36123, 36480-36481]`
- **Software**:
  - General (startup view, sounds, now-playing idle, screen graphics, warnings reset, update check frequency).
  - Collection (monitored folders, save folders, media info, delete behavior, ratings).
  - File Types.
  - Podcasts.
  - Pictures (slideshow speed).
  - Rip.
  - Burn.
  - Display.
  - Sharing.
  - (Excluded: Feedback program, Privacy, Account.)
- **Device / Phone**: Sync options, Sync groups, Conversion settings, Device name, Wireless sync, Summary. (Excluded: Update, Restore.)
- Settings changes are committed on leaving, with a "save your changes?" prompt `[STR:36518]`.

### 2.15 First launch `[UIID:FIRSTLAUNCH*]`, `[STR:36151-36155, 36419-36420]`
- Welcome screen: "start playing" with defaults, or "change the default settings".
- The settings path covers media locations (including external drives), media types and file types.

---

## 3. Inferred design (not directly observed; reasoned from §2 plus general knowledge)

### 3.1 Architecture
- Layered design: UI shell → collection/data-provider layer → local relational DB with a query processor → metadata extractor → codec/encoder services → device transport.
- *Basis*: component names and descriptions in §1.1 and the three trace switches.

### 3.2 Library data model (proposed for the reimplementation)
| Entity | Key fields |
|---|---|
| `MediaFile` | id, path, size, mtime, mediaType (audio/video/picture/podcastEpisode), mime, duration, bitrate, dateAdded, dateLastPlayed, playCount, rating(-1/0/+1), monitoredFolderId |
| `Track` (audio) | fileId, title, trackNo, discNo, albumId, contributingArtistIds[], composer, genreId, year, publisher, conductor |
| `Album` | id, title, albumArtistId, year, genreId, artPath |
| `Artist` | id, name, sortName |
| `Genre` | id, name |
| `Video` | fileId, title, category(TV/Music/Movie/Other), year, genre, description, series |
| `Picture` | fileId, dateTaken, folderAlbumId, width, height |
| `PodcastSeries` / `Episode` | feedUrl, title, keepCount, order, syncPolicy / guid, enclosureUrl, downloaded, playState |
| `Playlist` | id, name, kind(static/auto/burn/nowPlaying), items[] or rules |
| `AutoRule` | field, op(include/exclude/lt/gt/range/inLastDays), values[] ; plus arrange, limit(type,count) |
| `MonitoredFolder` | id, path, mediaType, isSaveFolder |
| `QuickplayPin` | entityType, entityId, pinnedAt |
| `Device` / `SyncGroup` | id, name, relationship(linked/guest), per-type mode; groupId, sourceRef, status |

- *Basis*: columns (§2.3), autoplaylist fields (§2.5), sync groups (§2.12) and identifiers like `UserPlayCount`, `DateAdded` and `AlbumArtistID` `[UIID]`.

### 3.3 File formats
- **.zpl**: a SMIL-style XML playlist (a `<smil><head>…</head><body><seq><media src=…/></seq></body></smil>` shape). This is from **public knowledge**, not observed in the installer. Implement read and write.
- **.m3u**: the plain/extended form (`#EXTM3U`, `#EXTINF`). Those markers appear as strings in `ZuneNativeLib.dll` (observed). Implement read and write.

### 3.4 Device sync in a reimplementation
- Real Zune and Windows Phone 7 devices need MTPZ authentication before sync. That is **excluded**, so the new tool cannot sync to genuine Zune hardware.
- See §4.1 for the full reasoning.
- The proposed target is **generic devices**: USB mass-storage players or SD cards (a folder target), and optionally standard MTP devices through libmtp. The full sync-mode, sync-group, queue, overfill and transcode logic in §2.12 applies unchanged.

### 3.5 Online metadata
- The original used Microsoft services (offline now).
- The proposed replacement is the **MusicBrainz** (and Cover Art Archive) public APIs for "Find album info", CD lookup by disc ID, and album art.
- Smart DJ is optional and local-only: "similar songs" by shared artist, genre, year and rating heuristics.

---


## 4. Wishlist assessment (Eric's list, 2026-11-18)

Each item from the wishlist was checked against the Phase 1 rule: no licensing, DRM, activation, key or authentication work.

**Verdicts:**
- **Yes**: in scope for this project.
- **Partial**: can be built, but only the part that stays inside the rule.
- **No**: blocked by the rule.
- **N/A**: hardware or a service business, not desktop software.

### 4.1 The key constraint: real Zune hardware
- Every Zune model (4/8/16, 30, 80/120, HD) refuses MTP file access until the host completes **MTPZ authentication**.
- MTPZ is a Microsoft device-authentication handshake that needs Microsoft's private key material. libmtp's "MTPZ support" works only when the user supplies keys extracted from Microsoft's software.
- Building or using that means circumventing an authentication/protection mechanism, which falls under the "auth" part of the rule.
- So this replacement **cannot read from or write to a genuine Zune**. That includes sync, backup, pulling files off the device, firmware install and Zune HD app install.
- What it *can* do with a Zune:
  - Detect one by its USB vendor/product IDs.
  - Name the model.
  - Explain plainly that a Zune needs the official software or a separately obtained tool.
- If Eric wants Zune-hardware access, that has to be a separate decision outside this clean-room project's rules.

### 4.2 Verdict table
| # | Wishlist item | Verdict | Notes |
|---|---|---|---|
| 1 | Sync without Zune software / drag-and-drop over USB | **Partial** | Full sync engine (all / chosen / manual, sync groups, queue, overfill) for USB mass-storage players, SD cards and non-MTPZ MTP players. Not genuine Zunes (§4.1). |
| 1 | Detect every Zune model | **Partial** | Detect and identify only, with no data access. |
| 1 | Speak MTP + MTPZ | **No** (MTPZ) / **Yes** (plain MTP) | Plain MTP through libmtp for generic players. |
| 1 | Storage use by category | **Yes** | For supported targets. |
| 1 | Back up / pull every file off a Zune | **No** | Needs MTPZ. Backup and restore of a *generic* device or folder is **Yes**. |
| 1 | Firmware updates from archived images | **No** | Device update and restore were already excluded in §0.3. |
| – | Work on Windows 10/11, macOS, Linux | **Yes** | Qt 6 is cross-platform, and the app needs no kernel drivers. |
| 2 | Watch folders, auto-import | **Yes** | Already in the spec (§2.2). |
| 2 | Tag read/edit incl. batch (ID3, Vorbis, MP4) | **Yes** | TagLib. |
| 2 | MusicBrainz / Cover Art Archive / Discogs | **Yes** | MusicBrainz and CAA have free public APIs. Discogs needs a user-supplied API token, so it's optional. |
| 2 | Duplicates, missing art/tags report | **Yes** | |
| 2 | Search / sort / filter | **Yes** | |
| 2 | Import from iTunes XML, foobar2000, M3U | **Yes** | iTunes Library.xml and M3U/PLS. foobar2000 through its exported playlists. |
| 3 | Two-way play counts / ratings | **Partial** | Possible only where the device exposes them (for example Rockbox databases). Not for Zunes. |
| 3 | Smart fill | **Yes** | |
| 3 | On-the-fly transcode with cache | **Yes** | FFmpeg, with targets MP3/AAC/Opus/FLAC/WMA (FFmpeg `wmav2`). |
| 3 | Video convert per device profile | **Yes** | Profiles are user-defined, not tied to Zune hardware. |
| 3 | Photo resize and sync by album | **Yes** | |
| 3 | Wi-Fi sync | **Partial** | No Zune Wi-Fi sync (that runs over the authenticated Zune protocol). Sync to a network share or folder is doable, and a phone companion app is a separate project. |
| 3 | Sync profiles for multiple devices | **Yes** | |
| – | Play FLAC / OGG / Opus natively | **Yes** | Desktop playback through FFmpeg/Qt Multimedia. |
| 4 | Smart playlists | **Yes** | Already in the spec (§2.5). |
| 4 | Smart DJ rebuild | **Yes** | Local heuristics, with optional Last.fm similar-artist data (needs a user API key). |
| 4 | Mixview (related-artists web) | **Yes** | MusicBrainz relationships / Last.fm. Original visuals, not copied. |
| 4 | M3U / PLS / XSPF / ZPL import-export | **Yes** | |
| 5 | Gapless, ReplayGain, mini-player | **Yes** | |
| 5 | Lyrics from .lrc | **Yes** | Local .lrc and embedded tags. A licensed lyrics source is out of scope. |
| 5 | Media keys, SMTC, macOS Now Playing, MPRIS | **Yes** | Per-OS integration. |
| 6 | Podcasts via RSS, Podcast Index / iTunes search | **Yes** | The Podcast Index API needs a free key. The iTunes Search API is public. |
| 6 | Auto-download, keep N | **Yes** | Already in the spec (§2.9). |
| 6 | Resume position sync device↔desktop | **Partial** | Desktop resume is **Yes**. Device resume is only where the device exposes it. |
| 6 | Audiobooks with chapters | **Yes** | For DRM-free files (M4B chapters, MP3 chapter frames). |
| 6 | Audible sync | **No** | Audible files are DRM-protected. |
| 6 | Pocket Casts sync | **No** | Pocket Casts has no public API. OPML import/export is **Yes**. |
| 7 | Community Zune servers (Marketplace look, artist pages) | **Partial** | Read-only artist and album info pages from a user-configured community server are possible. Anything account, purchase, Zune Pass or licensing related stays **No**. |
| 7 | Last.fm / ListenBrainz scrobbling | **Yes** | Desktop plays. Device plays only where the device logs them (for example Rockbox). |
| 7 | Shareable profile card | **Yes** | Generated locally as an image or HTML page. Optional ListenBrainz-backed version. |
| 7 | Discord Rich Presence | **Yes** | |
| 8 | Zune HD app install / homebrew | **No** | Needs the authenticated Zune protocol (§4.1). |
| 8 | Weather/news proxy for Zune HD | **No** | Same reason. |
| 9 | Metro-inspired look, pivots, big lowercase type, artist backgrounds from library | **Yes** | An original design *inspired by* the style, using open fonts (for example Open Sans / Inter). No Microsoft assets, fonts or bundled backgrounds. |
| 9 | Light/dark + accent colors | **Yes** | |
| 9 | Era skins (1.x/3.x/4.x) | **No** (as recreations) / **Yes** (as original themes) | Exact recreations of Microsoft's UI would copy its look, so only original themes are offered. |
| 10 | Open source, plugin system | **Yes** | Plugins for metadata sources, transcoders and scrobblers. |
| 10 | Portable mode | **Yes** | |
| 10 | Logs + "device not detected" troubleshooter | **Yes** | |
| 10 | Accessibility (screen reader, keyboard, scalable text) | **Yes** | Qt accessibility. |
| – | Streaming (Spotify / Apple Music / Tidal) with offline downloads | **No** | Offline downloads are DRM. Embedding these services needs their licensing. At most, a "now playing from Spotify" display through the OS media session is **Partial**. |
| – | Bluetooth/LDAC/aptX, USB-C, battery, DAC, OLED, touchpad, microSD | **N/A** | Hardware. The desktop app can show hi-res/bit-perfect info and do WASAPI exclusive output (**Yes**). |

---

## 5. Proposed Phase 2 scope (revised; needs Eric's approval)

**Stack:**
- C++20, Qt 6 (Widgets or QML), TagLib, SQLite, FFmpeg, libmtp (plain MTP only), CMake.
- Open source license suggestion: GPL-3.0, which is compatible with the libraries above.
- The first build is a Linux binary from this container. A Windows `.exe` is built on Eric's PC (Remote Control) or by MinGW cross-compile. macOS needs a Mac.

**Phase 2a, MVP:**
1. Library: watched folders, scanning, SQLite schema (§3.2), duplicates and missing-tag/art report, imports (iTunes XML, M3U/PLS/XSPF/ZPL).
2. Views: Artists, Albums, Genres, Songs (selectable columns), Playlists; search, sort and filter.
3. Tag editing (single and batch), merge-by-drag, album art paste, MusicBrainz + Cover Art Archive lookup.
4. Playlists: static, autoplaylists (full rule set), Now Playing queue, Quickplay pins and recents.
5. Playback: FLAC/Ogg/Opus/MP3/AAC/WMA, gapless, ReplayGain, mini-player, media keys / MPRIS / SMTC.
6. Device sync v1: folder / USB mass-storage targets, manual drag-and-drop and "sync all / items I choose", transcode with cache, storage-by-category view, Zune detection notice (§4.1).
7. Metro-inspired original UI with light/dark and accent color; settings pages; logs; portable mode.

**Phase 2b:**
- Plain MTP targets, sync groups and profiles, smart fill, photo resize, video profiles.
- Podcasts (RSS, search, keep N, OPML), audiobooks with chapters, .lrc lyrics.
- Scrobbling, Discord Rich Presence.

**Phase 2b additions (Eric approved 2026-11-19), after the Windows build:**
- **Gapless playback.** Use a custom decode-and-mix audio engine (FFmpeg decode into one QAudioSink stream), so the next song starts with no gap. Read encoder delay and padding (LAME/iTunNORM/iTunSMPB) so gapless albums play cleanly. This fixes the known gap in 0.1.0.
- **ReplayGain, extended.** 0.1.0 applies existing ReplayGain tags (track gain only). Add a track/album/off mode, a pre-amp and clipping protection, and a "calculate loudness" job for songs without tags (FFmpeg ebur128, written back as REPLAYGAIN_* tags).
- **Automatic album art and lyrics.** A background job finds missing covers through MusicBrainz and Cover Art Archive, using the existing lookup code, and saves them as embedded or folder art (user's choice). Lyrics are fetched from LRCLIB (free, open API, synced .lrc where available), shown in Now Playing with line-by-line highlighting, and cached next to the file as .lrc. Both can be turned off, and both respect rate limits.
- **Duplicate finder, extended.** 0.1.0 reports duplicates by matching tags. Add exact-file matching (size plus content hash), a "same song, different format/bitrate" group, and a review screen that keeps the best copy (highest quality, has art, most plays). It moves the rest to the trash or removes them from the collection, and play counts and ratings are merged into the kept copy.
- **Internet radio.** A new Radio view: search stations through the Radio Browser community directory (open API), favorites, add a stream by URL, read M3U/PLS station files, and show ICY now-playing titles. Optionally, played titles can be logged to history.
- **Discover view.** Uses local listening history only: "back in rotation" (loved but not played lately), "you haven't played" (added but never played), top artists/albums by period, artists similar to your favorites (MusicBrainz tags/relationships; Last.fm optional with the user's own key), and "on this day" listening.

**Phase 2c:**
- Smart DJ (local + optional Last.fm), Mixview, profile card.
- Plugin system, read-only community-server artist pages, CD rip/burn (§2.10–2.11, with MP3/FLAC/Opus in place of WMA-only), UPnP ContentDirectory sharing.

**Never built:** §0.3 exclusions plus the **No** rows in §4.2 (MTPZ, Zune device data access, firmware, Zune HD apps, Audible, streaming offline downloads, UI recreations).
