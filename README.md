# UltimateMangaReader

A feature-rich manga and light novel reader for **Kobo E-Ink devices** and desktop, based on Qt5.

Originally created by [Rain92](https://github.com/Rain92/UltimateMangaReader).  
Modernized and extended by [grabercn](https://github.com/grabercn).

---

## Features

### Reading
- **Manga reader** with tap zones, swipe gestures, and Kobo page button support
- **Light novel reader** with paginated text display, chapter navigation, and inline image support
- **Page preloading** — configurable number of pages and chapters preloaded ahead for seamless reading
- **Auto-download** next chapters in the background while you read
- **Bookmarks** — save specific pages to return to later
- **Reading progress** saved automatically and displayed on the Continue button

### Sources
- **MangaDex** — 70,000+ manga via API with multi-language title search
- **MangaPlus** — official Shueisha/Jump titles via protobuf API
- **MangaTown** — large HTML-scraped manga directory
- **Internet Archive (Manga)** — thousands of archived manga (CBZ/PDF)
- **Internet Archive (Novels)** — archived light novels (EPUB/PDF/TXT)
- **AllNovel** — light novels with chapter text reader

### Search
- **Live search** across all sources in parallel — no catalog download needed
- **Multi-language matching** — searches English, Japanese, and Romaji titles simultaneously
- **Smart ranking** — exact match, substring, word overlap with punctuation normalization
- **Alt title display** — shows Japanese/Romaji names found during search

### AniList Integration
- **Login via token** (works on Kobo without a browser)
- **Auto-tracking** — starts tracking when you begin reading, updates chapter-by-chapter
- **Progress sync** — AniList progress syncs to local on every manga open
- **Offline queue** — edits made while offline sync automatically when back online
- **Fuzzy title matching** — aggressively links AniList entries to cached manga via English, Romaji, and word overlap
- **Background matching** — Reading entries matched instantly, rest matched in background batches
- **Management screen** — view all lists, refresh, logout from Menu > AniList

### Downloads & Offline
- **Download chapters** for offline reading with progress tracking
- **Export to Kobo** — manga as image folders, light novels as HTML files
- **Download manager** — full-page queue view with progress, cancel, and management
- **Downloaded content browser** — view cached manga with chapter/page counts, delete with checkboxes
- **Download indicators** — `[DL]` prefix on downloaded chapters, badge count on header icon

### Favorites & History
- **Favorites** with smart status (Not started / Reading / Finished) and AniList integration
- **Browsing history** — tracks all manga you've viewed with timestamps
- **Reading stats** — chapters read, pages read, total time, reading streak
- **Recent searches** — last 3 shown on home page with "View history" link

### UI & Design
- **Modern e-ink optimized theme** — high contrast monochrome, large touch targets
- **Welcome screen** — multi-page intro on first boot
- **Sleep screen** — shows current manga cover, reading progress, time, and battery
- **Loading spinner** — animated indicator when loading manga
- **Full-screen settings** — with Save/Back bottom bar
- **Configurable auto-sleep** — 5/10/15/30/60 minutes or never
- **WiFi auto-disconnect on sleep** — saves battery
- **Color mode** — disable greyscale conversion for color e-readers or desktop
- **Frontlight control** — brightness and comfort light sliders (Kobo)

### Platform Support
- **Kobo Libra H2O** — primary target, optimized for 7" 1264x1680 e-ink touchscreen
- **Windows** — full desktop build with parallel compilation (jom)
- **Linux** — native desktop build via qmake
- **Docker** — Dockerfile for Kobo ARM cross-compilation

---

## Install on Kobo

1. Install a launcher: [KFMon](https://github.com/NiLuJe/kfmon) ([instructions](https://www.mobileread.com/forums/showthread.php?t=274231))
2. Download the latest release and extract to the root of your Kobo device

---

## Build

### Requirements
- Qt 5.15+ with OpenSSL 1.1.1+
- libjpeg-turbo, libpng

### Windows (Quick Start)
```powershell
# One-time setup
.\setup-windows.ps1

# Build (incremental, uses jom for parallel compilation)
.\build-win.bat

# Clean rebuild
.\rebuild-win.bat

# Run
.\run.bat
```

### Linux Desktop
```bash
./build.sh desktop
```

### Kobo (Docker Cross-Compile)
```bash
./build.sh kobo
```

The Docker build sets up the full ARM cross-compilation toolchain, builds Qt 5.15 for Kobo, and compiles the application. Output binary goes to `output/`.

### Kobo (Manual)
See the original [cross-compilation guide](https://github.com/Rain92/qt5-kobo-platform-plugin) for manual setup with koxtoolchain.

Add `CONFIG+=kobo` to qmake arguments and place the platform plugin source in the same parent folder.

---

## Architecture

| Component | Description |
|-----------|-------------|
| Sources | Extend `AbstractMangaSource` with `searchManga()`, `getMangaInfo()`, `getPageList()`, `getChapterText()` |
| Content Types | `ContentManga` (image pages) and `ContentLightNovel` (text chapters) |
| Network | `NetworkManager` with custom headers, cookies, timeout handling, and offline mode |
| AniList | GraphQL API with Bearer token auth, debounced tracking, offline queue |
| Settings | Binary QDataStream serialization with backwards-compatible optional fields |
| UI | Qt Widgets with e-ink optimized QSS stylesheet, touch-friendly sizing |

---

## Credits

- **Original Author:** [Rain92](https://github.com/Rain92)
- **Modernized & Extended:** [grabercn](https://github.com/grabercn)
- **Third-party:** rapidjson, picoproto, simdimageresize

## License

See [LICENSE](LICENSE) for details.
