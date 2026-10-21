# Checks and known findings

## Automated checks

| Check | What it verifies |
|---|---|
| [CI](https://github.com/Take-No-Break/Choosen/actions/workflows/ci.yml) | Builds the Windows app and bundled interfaces, then runs the configured application self-tests. |
| [CodeQL](https://github.com/Take-No-Break/Choosen/actions/workflows/codeql.yml) | Analyzes C++, Java and JavaScript/TypeScript source. Findings appear in [code scanning](https://github.com/Take-No-Break/Choosen/security/code-scanning). |
| [Release checks](https://github.com/Take-No-Break/Choosen/actions/workflows/release-checks.yml) | Checks published download sizes, SHA-256 hashes, archive contents and documentation links. |

The README badges show workflow execution status on the public main branch. A successful CodeQL run means analysis completed; unresolved findings are listed separately below and in code scanning.

## Download results

These VirusTotal results were recorded on 2026-10-21 for the exact **0.16.0** files identified by SHA-256 below. The Android APK and the EXE extracted from the published Windows ZIP were submitted. The ZIP itself was not submitted for a separate malware scan.

| File | Result |
|---|---|
| 0.16.0 Android APK | [0 / 67 detections](https://www.virustotal.com/gui/file/4fdd3df8fe6780935c63f3bcebd3093cc11476d03a870e861fc6e98f81335b79/detection) |
| 0.16.0 Windows EXE inside the ZIP | [2 / 71 detections — investigation unresolved](https://www.virustotal.com/gui/file/4014d703b1c9f5e6ab607319d4f73e281e9c04f7a417a03f0a16d08cc8fa33b0/detection) |

For the 0.16.0 EXE, Microsoft reported **Trojan:Win32/Wacatac.B!ml** and SecureAge reported **Malicious**. Whether these are false positives has not been established. Avoid running the Windows binary until investigated.

The APK report also lists two timeouts (Skyhigh and TEHTRIS) and six engines unable to process the file type. Those outcomes are not clean results. Counts and verdicts can change; the linked reports are authoritative. No result is a guarantee of safety, and results for other versions do not apply to these files.

## Source finding

MainActivity.pcAddress contains a potentially slow regular expression for oversized PC-address input (java/polynomial-redos, high severity in CodeQL). The finding is unresolved. Android device timing, normal-screen reachability and remote exploitation have not been established.

CodeQL no-build analysis does not cover Kotlin or Swift, and Svelte template coverage is not promised. A passing check means the configured operation completed, not that the app is malware-free.

## Download integrity

Release checks verify the published files' sizes, SHA-256 hashes, archive contents and documentation links. They do not execute the app or detect malware. The supplied APK uses its existing development signing certificate; Windows has no commercial code-signing certificate. Preserve ratings when updating: do not uninstall an existing Android app just to force a different-signature update.

| File | SHA-256 |
|---|---|
| Choosen-Android-arm64.apk | 4fdd3df8fe6780935c63f3bcebd3093cc11476d03a870e861fc6e98f81335b79 |
| Choosen-Windows-x64.zip | 660c9e7c578fd542f85dc640adc1ede3366a7d954d70129658b9597c05aa60c1 |
| Choosen.exe inside the ZIP | 4014d703b1c9f5e6ab607319d4f73e281e9c04f7a417a03f0a16d08cc8fa33b0 |
