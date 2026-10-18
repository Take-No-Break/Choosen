# Checks and known findings

## Automated checks

| Check | What it verifies |
|---|---|
| [CI](https://github.com/Take-No-Break/Choosen/actions/workflows/ci.yml) | Builds the Windows app and bundled interfaces, then runs the configured application self-tests. |
| [CodeQL](https://github.com/Take-No-Break/Choosen/actions/workflows/codeql.yml) | Analyzes C++, Java and JavaScript/TypeScript source. Findings appear in [code scanning](https://github.com/Take-No-Break/Choosen/security/code-scanning). |
| [Release checks](https://github.com/Take-No-Break/Choosen/actions/workflows/release-checks.yml) | Checks published download sizes, SHA-256 hashes, archive contents and documentation links. |

The README badges show workflow execution status on the public main branch. A successful CodeQL run means analysis completed; unresolved findings are listed separately below and in code scanning.

## Download results

The Android APK and Windows ZIP are the distributed application files. The recorded VirusTotal results for these exact files are:

| File | Result |
|---|---|
| Android APK | [0 / 68 detections](https://www.virustotal.com/gui/file/481ecbebb462164acca8cefffd77e0bfcafd9a4f27443f5892f2cb7b31be45e5/detection) |
| Windows EXE inside the ZIP | [2 / 71 detections — investigation unresolved](https://www.virustotal.com/gui/file/f2e103eba8543db986136a5760b3bb4efb26bb71811b509a9354ba132cba1cda/detection) |

Results were recorded on 2026-10-15 and can change. Microsoft reported Trojan:Win32/Wacatac.B!ml; SecureAge reported Malicious. Whether these are false positives has not been established. Avoid running the Windows binary until investigated. No result is a guarantee of safety.

## Source finding

MainActivity.pcAddress contains a potentially slow regular expression for oversized PC-address input (java/polynomial-redos, high severity in CodeQL). The finding is unresolved. Android device timing, normal-screen reachability and remote exploitation have not been established.

CodeQL no-build analysis does not cover Kotlin or Swift, and Svelte template coverage is not promised. A passing check means the configured operation completed, not that the app is malware-free.

## Download integrity

Release checks verify the published files' sizes, SHA-256 hashes, archive contents and documentation links. They do not execute the app or detect malware. The supplied APK uses its existing development signing certificate; Windows has no commercial code-signing certificate. Preserve ratings when updating: do not uninstall an existing Android app just to force a different-signature update.

| File | SHA-256 |
|---|---|
| Choosen-Android-arm64.apk | 481ecbebb462164acca8cefffd77e0bfcafd9a4f27443f5892f2cb7b31be45e5 |
| Choosen-Windows-x64.zip | 41896957f3068d05122a182ca2ce5b69cd6bbaefb0086ef7d0bf3ac68797274b |
| Choosen.exe inside the ZIP | f2e103eba8543db986136a5760b3bb4efb26bb71811b509a9354ba132cba1cda |
