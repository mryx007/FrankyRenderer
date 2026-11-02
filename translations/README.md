# Translations

The settings menu is translated with gettext catalogues, maintained on Weblate.

- `FrankyRenderer.pot` is the template: every English menu text with a note on where it appears.
  It is generated; do not edit it by hand.
- `<language>.po` files (`de.po`, `fr.po`, `pt_BR.po`, ...) hold the translations. Weblate creates and updates them.

English is the source text and stays the fallback: anything untranslated, empty or marked
fuzzy is shown in English. The menu follows the Windows display language by default; users can
pick a language under General > Themes > Language.

## Contributing a translation

Translate on [Weblate](https://hosted.weblate.org/projects/frankyrenderer/). Only translate the
text; keep placeholders such as `%d` and `%s` and keep technical names (filter names such as
`ewa_lanczos`) unchanged.

## Maintainers

After changing menu texts, regenerate the template and commit it:

```
python src/Tools/translations.py template
```

Weblate picks up the new template and marks changed texts for review.

The renderer build (`src/Tools/build.cmd`) embeds every `translations/*.po` file that is at
least 95% translated into `FrankyRenderer.ax`; no extra files are shipped. Weblate pull requests
can therefore be merged at any time: a language appears in the menu once it reaches 95%. The
build log lists the share of every language.

Weblate PRs containing only added or modified `translations/<language>.po` files
are validated automatically, then approved for the normal Code checks workflow
(including the Windows build). After those checks succeed, the PR is squash-merged.
The automation checks Weblate's account and fork, the exact PR revision, UTF-8 PO
syntax and printf placeholders. Other files, deletions, conflicts and failed
checks require manual review. Incomplete languages can be merged; the 95% build
threshold is unchanged. Merging updates source only, without publishing a Nightly
or installing the renderer. Approval is checked every 15 minutes (GitHub may delay
scheduled runs); check completion also triggers the automation immediately.

### Weblate component settings

| Setting | Value |
|---|---|
| File format | gettext PO file |
| File mask | `translations/*.po` |
| Template for new translations | `translations/FrankyRenderer.pot` |
| Monolingual base language file | (empty) |
| Source language | English |
| Adding new translation | Create new language file |
