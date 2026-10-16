# Choosen 📸

[![Release checks](https://github.com/Take-No-Break/Choosen/actions/workflows/release-checks.yml/badge.svg)](https://github.com/Take-No-Break/Choosen/actions/workflows/release-checks.yml) · [MIT license](LICENSE) · [Checks & scan results](SECURITY-CHECKS.md)


**Swipe your photos, Tinder-style. Find your favorites, then organize them.**

Choosen helps you work through a photo collection one picture at a time. Choose your folders, rate the photos, and come back to your favorites in the gallery. When you're ready, turn those ratings into real folders on your device.

🌐 [日本語](docs/ja.md) · [中文](docs/zh-CN.md) · [English](docs/en.md) · [ไทย](docs/th.md) · [Português](docs/pt-BR.md)

[⬇ Android APK](https://github.com/Take-No-Break/Choosen/releases/download/v0.12.0/Choosen-Android-arm64.apk) · [⬇ Windows ZIP](https://github.com/Take-No-Break/Choosen/releases/download/v0.12.0/Choosen-Windows-x64.zip)

Android 10+ / ARM64 · Windows x64 + Microsoft Edge

⚠️ Windows: VirusTotal reported **2/71 detections**; investigation is pending. Android: **0/68**. [Read the results before downloading](SECURITY-CHECKS.md).


## Start with your photo folders

Open an existing photo folder to start swiping. You can add more than one folder and include the subfolders inside it, so a collection can stay organized the way you already keep it.

- **Choose what to rate:** use the folder button on the swipe screen to turn folders on or off.
- **Go deeper:** choose **Choose by subfolder** to narrow your session to particular subfolders.
- **Change your library:** open **Settings → Folders** to add or remove source folders and choose whether to include their subfolders.

Adding a source folder tells Choosen where to look for photos. Removing it from the library leaves the original files and their saved ratings intact.

## Rate first, organize later

Swipe through your collection and decide how much you like each photo:

| Rating | Use it for |
|---|---|
| ♥ **Liked** | Photos you want to keep coming back to. |
| ★ **Super liked** | Your standout favorites. |
| **Passed** | Photos you don't want in your favorites. |

You can change a rating later. Rating a photo doesn't move its original file, so you can finish choosing your favorites before reorganizing anything.

![Swipe through your photos and rate your favorites](images/swipe.jpg)

## Find your favorites in the gallery

Open **Gallery** to browse your liked and starred photos together or separately. Filter by folder, change the sort order, or open a photo for a closer look. You can also revisit passed and unrated photos.

Want a smaller collection? Long-press a photo to start selecting, then use **Add to the sub-gallery** to bookmark your selection. The **Sub-gallery** is a collection inside Choosen: the original files stay where they are.

![Browse and filter your photo collection in the gallery](images/gallery.jpg)

## Turn ratings into real photo folders

After rating, you can let Choosen sort the original files into subfolders on **Android or Windows**:

1. In **Gallery**, long-press to select photos, then choose **Subfolders**. To sort a source folder instead, open **Settings → Sort into subfolders by rating**.
2. Choose the rating groups you want to organize and review the photo counts.
3. Choose **Move** to place those originals into rating folders inside each photo's existing folder.

Choosen creates these folders as needed. Their names currently stay in Japanese, including when the interface is in English:

```text
Your photo folder/
├── 好評/       Liked ♥
├── 超好評/     Super liked ★
└── 見送り/     Passed
```

Your ratings follow the photos, and you can undo the last sorting operation. On Android, moving files may require **All files access** permission. Organize files on the device that holds the library; these controls aren't available while a phone is viewing a PC library remotely.

### Copy favorites to another destination — Windows

For a separate collection of copies, open **Settings → Export** in the Windows app. Choose a rating group, select **Copy**, choose a **Destination** outside your library folders, then choose **Export** and confirm.

Choosen creates a dated collection folder in that destination, preserves the relative folder structure, and leaves your originals in place. This is useful for gathering your liked or starred photos for another project.

Windows Export can also **Move** passed or archived photos out of the library. Moving liked or starred originals uses the **Sort into subfolders** feature above. The desktop Export controls aren't available in the Android app.

## See which collections you love most

Open **Ranking** to compare your folders using the ratings you've given their photos. It's a quick way to see which collections keep producing your favorites.

![Compare your photo collections in Ranking](images/ranking.jpg)

## Enjoy your collection

Use **Gallery Play** to display several photos together, or **Slides** for horizontal playback. Adjust how many pictures appear and the playback timing to suit the way you want to browse.

![Enjoy multiple photos together in Gallery Play](images/play.jpg)

The screenshots show the actual Windows app with generated demo images. Android adapts the layout to your screen.

## Install and use

- **Android:** download the APK above on your phone and open it to install. Android may ask you to allow installation from the browser or file app you used.
- **Windows:** download and unzip the Windows ZIP, then open **Start-Choosen.cmd**. Microsoft Edge is required.

Your photo library and ratings stay on your device. You can also pair a phone with your own PC to browse its library. The app interface supports English, Japanese and Brazilian Portuguese; the guides above are available in five languages.

[Feedback / Issues](https://github.com/Take-No-Break/Choosen/issues) · [Releases](https://github.com/Take-No-Break/Choosen/releases) · [Third-party notices](THIRD-PARTY-NOTICES.txt)

Choosen is independent and is not affiliated with Tinder. This is the home of the app: downloads, source code and feedback. [Source code is public under MIT](https://github.com/Take-No-Break/Choosen) · [Application source](https://github.com/Take-No-Break/Choosen/tree/main). Third-party components retain their own licenses.

## Source and development

| Directory | Purpose |
|---|---|
| [src](src/) | Photo library, ratings, ranking and Windows host. |
| [web3](web3/) | Svelte interface for swiping, ranking and gallery playback. |
| [android](android/) | Android app, WebView integration and shared native core. |
| [docs](docs/) | App guides and technical documentation. |
| [checks](checks/) | Download integrity and documentation verification. |

[Application source](https://github.com/Take-No-Break/Choosen/tree/main) · [Architecture and build guide](docs/ARCHITECTURE.md) · [Documentation](docs/README.md)

## Contributors

- **[Bruno Eric / Take-No-Break](https://github.com/Take-No-Break)** — product direction, feature decisions and hands-on testing.
- **Claude Opus (Anthropic AI)** — implementation, bug fixes and code review.
- **Codex (OpenAI AI)** — implementation, independent verification, releases and documentation.

[Contribution details](CONTRIBUTORS.md)
