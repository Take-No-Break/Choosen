# Release integrity checks

Verifies the published Android and Windows downloads without executing them. The manifest specifies expected sizes, SHA-256 hashes and archive contents; the verifier also checks documentation links.

- [release.json](release.json): download identity and expected payloads.
- [verify_release.py](verify_release.py): integrity and documentation verification.
- [test_verify_release.py](test_verify_release.py): rejection cases for altered or unexpected files.

These checks are separate from application tests and malware analysis. See [checks and known findings](../SECURITY-CHECKS.md).
