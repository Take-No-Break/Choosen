# Architecture and build guide

This repository contains the application source used by the Android and Windows downloads.

## Project structure

| Location | Role |
|---|---|
| `src/` | Shared C++20 library: photo indexing, fingerprints, rating events, scheduling, ranking, export, and the Windows host/platform layer. |
| `web3/` | Svelte and TypeScript interface: swipe, ranking, gallery, slideshow, settings, and translations. Android opens it by default; Windows serves it at /v3/. |
| `web/` | JavaScript, HTML and CSS interface. Windows opens it at / by default; Android offers it as an alternative. |
| `android/` | Java Android shell with WebView, JNI, and the shared C++ library. The interface is bundled into the APK. |
| `app/`, `native/` | Separate Flutter/Dart client and C++ FFI library; not required by the Windows and Android builds described here. |
| `ios/` | iOS client source using Swift, Objective-C++, WKWebView and Photos. Development and distribution are on hold. |
| `checks/` | Verifies download integrity and documentation links; not part of the app runtime. |

## Build the applications

**Windows:** Visual Studio C++ Build Tools and Node.js are required. Run `build.bat` in the repository root. It builds the Svelte interface and embeds both web interfaces into `Choosen.exe`. The Windows host opens Microsoft Edge in an app window. Start Choosen.exe --ui v3 to use the Svelte interface; the Windows download launcher selects this mode.

**Android:** JDK 17, Android SDK/NDK, and Node.js are required. In `web3/`, run `npm ci` and `npm run build`. In `android/`, run `gradlew.bat assembleDebug`. The configuration targets ARM64 devices running Android 10 or later.

## Data storage

| File | Contents |
|---|---|
| `ratings.log` | Saved rating events. |
| `config.txt` | Selected folders and settings. |
| `index.tsv`, `index2.tsv` | Photo index caches. |
| `picks.tsv` | Saved slideshow favorites. |
| `backup/` | Rating backups. |

Windows stores these in `data/` beside `Choosen.exe`. Android uses its private `files/data/` directory. Removing a folder from the selection retains its rating events. The interface keeps pending ratings in a local outbox until the library confirms them.

## Further reading

[Download and screenshots](../README.md) · [Contributors](../CONTRIBUTORS.md) · [License](../LICENSE) · [Checks and scan results](../SECURITY-CHECKS.md)
