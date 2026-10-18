# Repository automation

Configuration for contributor checks and release-file verification.

- [workflows](workflows/): application CI, CodeQL and release-integrity workflows.
- [pull request template](pull_request_template.md): change summary, focused validation and review information.

Application CI runs for pull requests or manual requests. CodeQL also scans relevant source or scanner-configuration updates pushed to the main branch, so its findings stay visible in Security. Release-integrity checks also run when a release is published. No scheduled development task is defined here.
