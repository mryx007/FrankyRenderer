# GitHub automation

- **Nightly:** Source changes pushed to `main` in `FrankyRenderer-Nightly` build both packages. Formatting, non-GPU regression tests and ZIP integrity must pass before a separate normal Nightly release is created. Each build has a fixed date/commit tag; older releases, ZIPs and changelogs are preserved. Documentation-only changes do not publish another build. The workflow can also be started manually.
- **Stable:** Use **Publish stable release** on the Nightly page, then **Run workflow** in the main repository. No version or changelog input is needed. The workflow takes the published Nightly, keeps the main README and automation, calculates the next version and uses the short Nightly changelog. Both variants must build and pass the automated tests before the source and normal release are published. **Test without publishing** runs the preparation and build only. An already promoted Nightly or a Nightly without new renderer/build changes is rejected.
- **Code checks:** Pull requests and relevant source changes check formatting and build both variants. Tests requiring a real GPU, display or player remain manual.
- **libplacebo:** Every Monday, the stable repository compares the pinned version with the latest official release. A new version deliberately marks the check as failed, with a link in the run summary. GitHub Actions notifications must be enabled in your account to receive these alerts. No dependency is updated automatically.
- **Bug reports:** The bug-report form collects the player, renderer version and reproduction steps. Blank issues remain available.

Builds use Windows Server 2022 and Python 3.14. Existing checksum locks remain authoritative. FFmpeg headers come from the pinned upstream commit; the four pinned DLLs are extracted from the official MPC-HC 2.8.4 archive after checking both archive and file hashes. Nothing is installed or registered on a user's computer by these workflows.

Failed builds leave the published release untouched. Successful packages are also retained as workflow artifacts for seven days. Public release ZIPs do not expire with those artifacts.

Append concise user-facing changes to `docs/nightly-changelog.md`; keep its existing entries. Each Nightly shows only entries added since the previously published Nightly. Re-running the same build preserves its notes. Stable releases collect all entries since the last stable release, without duplicates. Git history, documentation commits and automation changes are not added to the changelog automatically.

The workflows run in their own repository using its built-in token. They require no personal access token and cannot publish stable releases from the Nightly repository. GitHub-hosted runs do not establish HDR/TV compatibility or real-player playback correctness.

Stable versions follow `1.0 → 1.0.1 → … → 1.0.9 → 1.1 → 1.1.1 → … → 1.1.9 → 1.2`. After `1.9.9`, the next version is `2.0`. The release workflow calculates the next version in this sequence and updates all resource/package versions together. It never skips ahead or overwrites an existing release.

Stable publication is restricted to the `mryx007` account (GitHub user ID `149878151`). Both the original actor and the account starting or re-running the workflow must match. Public visitors cannot start publication.
