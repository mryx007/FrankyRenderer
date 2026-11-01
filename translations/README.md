# Translations

The settings menu is translated with gettext catalogues, maintained on Weblate.

- `FrankyRenderer.pot` is the template: every English menu text with a note on where it appears.
  It is generated; do not edit it by hand.
- `<language>.po` files (`de.po`, `fr.po`, `pt_BR.po`, ...) hold the translations. Weblate creates and updates them.

English is the source text and stays the fallback: anything untranslated, empty or marked
fuzzy is shown in English. The menu follows the Windows display language by default; users can
pick a language under General > Themes > Language.

## Contributing a translation

Translate on Weblate (link in the main README once the project is set up). Only translate the
text; keep placeholders such as `%d` and `%s` and keep technical names (filter names such as
`ewa_lanczos`) unchanged.

## Maintainers

After changing menu texts, regenerate the template and commit it:

```
python src/Tools/translations.py template
```

Weblate picks up the new template and marks changed texts for review.

The renderer build (`src/Tools/build.cmd`) embeds every `translations/*.po` file into
`FrankyRenderer.ax`; no extra files are shipped.

### Weblate component settings

| Setting | Value |
|---|---|
| File format | gettext PO file |
| File mask | `translations/*.po` |
| Template for new translations | `translations/FrankyRenderer.pot` |
| Monolingual base language file | (empty) |
| Source language | English |
| Adding new translation | Create new language file |
