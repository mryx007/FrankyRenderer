# Nightly test builds

[Stable releases](https://github.com/mryx007/FrankyRenderer/releases/latest) contain confirmed changes. The [FrankyRenderer-Nightly repository](https://github.com/mryx007/FrankyRenderer-Nightly) is for fixes awaiting user confirmation, especially for hardware or input sources that the maintainer cannot test directly.

Download test packages from the separate [FrankyRenderer-Nightly repository](https://github.com/mryx007/FrankyRenderer-Nightly/releases/latest). Its releases do not appear in the main project's release list. There are two variants: MPC-BE for native FrankyRenderer selection, and PotPlayer for MPC Video Renderer selection. They use the same renderer identities as their stable counterparts; nightly is not a separately installed renderer.

Each ZIP name and its `INSTALL.txt` identify the build by date and commit, such as `20261029-abcdef0`. Include this ID, your player version and test results in the relevant issue. A successful build does not mean that the reported issue has been confirmed fixed on the affected hardware.

## Publishing a test build

Push tested source changes to `main` in `mryx007/FrankyRenderer-Nightly`. GitHub Actions checks formatting, builds both variants, runs the available non-GPU regression tests and verifies the packages before publishing a separate normal Nightly release with a fixed date/commit tag. Documentation-only changes do not trigger publication. The workflow can also be started manually from **Actions → Publish Nightly**.

Build failures leave the existing release untouched. Each release links the exact source commit and identifies both packages by date and commit. Hardware, HDR/TV and real-player tests still require user confirmation.

Previous builds and their changelogs remain available in the release history. After confirmation, use the manual **Publish stable release** workflow; it takes the latest published Nightly and collects all changelog entries since the previous stable release. See [GitHub automation](automation.md).
