# Nightly test builds

[Stable releases](https://github.com/mryx007/FrankyRenderer/releases/latest) contain confirmed changes. The [FrankyRenderer-Nightly repository](https://github.com/mryx007/FrankyRenderer-Nightly) is for fixes awaiting user confirmation, especially for hardware or input sources that the maintainer cannot test directly.

Download test packages from the separate [FrankyRenderer-Nightly repository](https://github.com/mryx007/FrankyRenderer-Nightly/releases/tag/nightly). Its releases do not appear in the main project's release list. There are two variants: MPC-BE for native FrankyRenderer selection, and PotPlayer for MPC Video Renderer selection. They use the same renderer identities as their stable counterparts; nightly is not a separately installed renderer.

Each ZIP name and its `INSTALL.txt` identify the build by date and commit, such as `20261029-abcdef0`. Include this ID, your player version and test results in the relevant issue. A successful build does not mean that the reported issue has been confirmed fixed on the affected hardware.

## Publishing a test build

1. Sync the current confirmed stable source into `mryx007/FrankyRenderer-Nightly` and apply proposed fixes there.
2. Build both variants and run the applicable checks. Record any tests that still require the reporter's device or source.
3. Package that exact source revision with `python src/package_variants.py --channel nightly --build-id YYYYMMDD-<commit> --output releases/<new-folder>`.
4. Create or update the `nightly` pre-release in `mryx007/FrankyRenderer-Nightly`, attach its two ZIP assets, and link the exact source commit in `mryx007/FrankyRenderer-Nightly`. Describe what needs testing. Publish only when an actual fix is ready for testing and keep it marked **Pre-release**.
5. After confirmation, cherry-pick the tested fix commits into the stable repository for a stable release. Keep the short nightly README separate from the main overview.

Publication is manual. No scheduled builds or automatic promotion to stable are configured. Nightly development and downloads are kept in the separate repository; the stable repository needs no nightly branch.
