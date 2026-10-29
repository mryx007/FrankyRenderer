# Nightly test builds

[Stable releases](https://github.com/mryx007/FrankyRenderer/releases/latest) contain confirmed changes. The [`nightly` branch](https://github.com/mryx007/FrankyRenderer/tree/nightly) is for fixes awaiting user confirmation, especially for hardware or input sources that the maintainer cannot test directly.

Download the two package variants from the [Nightly pre-release](https://github.com/mryx007/FrankyRenderer/releases/tag/nightly). Use the MPC-BE package for native FrankyRenderer selection, or the PotPlayer package for MPC Video Renderer selection. They use the same renderer identities as their stable counterparts; nightly is not a separately installed renderer.

Each ZIP name and its `INSTALL.txt` identify the build by date and commit, such as `20261029-abcdef0`. Include this ID, your player version and test results in the relevant issue. A successful build does not mean that the reported issue has been confirmed fixed on the affected hardware.

## Publishing a test build

1. Start from the current confirmed `main` state and put the proposed fix on `nightly`.
2. Build both variants and run the applicable checks. Record any tests that still require the reporter's device or source.
3. Package that exact source revision with `python src/package_variants.py --channel nightly --build-id YYYYMMDD-<commit> --output releases/<new-folder>`.
4. Update the `nightly` release tag to that revision, replace its two ZIP assets, and describe what needs testing. Keep the release marked **Pre-release**, without making it the latest stable release.
5. After confirmation, bring the tested changes into `main` for a stable release.

Publication is manual. No scheduled builds or automatic promotion to stable are configured. The old `native-clsid-preview` branch is superseded by `nightly`.
