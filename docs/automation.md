# GitHub automation

- **Nightly:** Source changes pushed to `main` in `FrankyRenderer-Nightly` build both packages. Formatting, non-GPU regression tests and ZIP integrity must pass before the normal Nightly release is updated. Documentation-only changes do not publish another build. The workflow can also be started manually.
- **Stable:** In the stable repository, open **Actions → Publish stable release → Run workflow**. Enter the version already committed in `Renderer.rc`, `package.py` and `package_variants.py`, plus short English release notes. The workflow checks the version, builds both packages and publishes only after the tests pass. Existing releases cannot be overwritten by this workflow.
- **Code checks:** Pull requests and relevant source changes check formatting and build both variants. Tests requiring a real GPU, display or player remain manual.
- **libplacebo:** Every Monday, the stable repository compares the pinned version with the latest official release. A new version deliberately marks the check as failed, with a link in the run summary. GitHub Actions notifications must be enabled in your account to receive these alerts. No dependency is updated automatically.
- **Bug reports:** The bug-report form collects the player, renderer version and reproduction steps. Blank issues remain available.

Builds use Windows Server 2022 and Python 3.14. Existing checksum locks remain authoritative. FFmpeg headers come from the pinned upstream commit; the four pinned DLLs are extracted from the official MPC-HC 2.8.4 archive after checking both archive and file hashes. Nothing is installed or registered on a user's computer by these workflows.

Failed builds leave the published release untouched. Successful packages are also retained as workflow artifacts for seven days. Public release ZIPs do not expire with those artifacts.

The workflows run in their own repository using its built-in token. They require no personal access token and cannot publish stable releases from the Nightly repository. GitHub-hosted runs do not establish HDR/TV compatibility or real-player playback correctness.
