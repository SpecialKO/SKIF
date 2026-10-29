# Translations of SKIF

SKIF uses gettext `.po` files, read directly by SKIF (no compiling, no third-party tools).

| File | What it is |
| --- | --- |
| `SKIF.pot` | The template: every translatable English text. Generated, do not edit by hand. |
| `<language>.po` | One translation per language, e.g. `de.po`, `pt_BR.po`, `zh_Hans.po`. |
| `..\..\l10n.rc` | Lists the `.po` files that are embedded in `SKIF.exe`. |
| `..\..\tools\l10n_extract.ps1` | Regenerates `SKIF.pot` from the sources. |

## Translating

1. Start a new language from `SKIF.pot` (Poedit: *File → New from POT/PO file*; Weblate does this for you), or open its existing `.po` file.
2. Set the `Language` header (e.g. `de`, `pt_BR`) and the `Plural-Forms` header of the language (Poedit and Weblate fill this in).
3. Keep in the translation:
   * `{0}`, `{1}`, ...: values filled in by SKIF. They may be moved around or left out, but no new ones may be added (a translation with unknown placeholders is ignored).
   * `[...]`: a clickable link (e.g. `[Library]`). Translate the text inside, keep the brackets.
   * `\n`: a line break.
4. Entries marked *fuzzy* and empty ones are shown in English.

To test a translation without building SKIF, put the `.po` file in `<Special K folder>\Languages\` (it takes priority over an embedded one of the same language), restart SKIF (files are read at startup), and pick the language in *Settings → Appearance → Language*. The log (`SKIF.log`) lists each loaded file, how much of it is translated, and any entries that were skipped.

## Maintaining

* After changing UI text, run `powershell -ExecutionPolicy Bypass -File tools\l10n_extract.ps1` to update `SKIF.pot`, then update each `.po` file from it (Poedit: *Translation → Update from POT file*; Weblate updates them on its own).
* To embed a new language, add a line to `l10n.rc`: `de SKIF_PO "resources\\l10n\\de.po"`.
* How to mark text for translation in the sources is described at the top of `include\utility\l10n.h`.
* The *Pseudo-locale* (listed in the language picker when *Settings → Advanced → Developer mode* is enabled) shows every translated text accented and ~35% longer, which makes text that is not marked for translation (it stays plain English) and layouts that are too narrow for longer languages easy to spot.
