# Translations of SKIF

SKIF uses gettext `.po` files, read directly by SKIF (no compiling, no third-party tools).

| File | What it is |
| --- | --- |
| `SKIF.pot` | The template: every translatable English text. Generated, do not edit by hand. |
| `<language>.po` | One translation per language, e.g. `de.po`, `pt_BR.po`, `zh_Hans.po`. |
| `en-x-pirate.po` | An example: a complete translation into Pirate English, not embedded. Try it via the `Languages` folder (see *Testing a translation*). |
| `..\..\l10n.rc` | Lists the `.po` files that are embedded in `SKIF.exe`. |
| `..\..\tools\l10n_extract.ps1` | Regenerates `SKIF.pot` from the sources. |

## Translating

1. Start a new language from `SKIF.pot` (Poedit: *File > New from POT/PO file*), or open its existing `.po` file. Any `.po` editor works; Poedit is free and shows the comments and contexts described below.
2. Set the `Language` header and the `Plural-Forms` header of the language (Poedit fills these in). The language is a BCP 47 tag; `_` and `-` are both accepted: `de`, `pt-BR` / `pt_BR`, `zh-Hans` / `zh_Hans`.
3. Read the comments of an entry (*Notes for translators* in Poedit). They say what the placeholders are and any limits.
4. Entries marked *fuzzy* and empty ones are shown in English.

To see what a finished translation looks like, open `en-x-pirate.po`. It uses a private-use tag (`en-x-pirate`) that Windows has no name for, so it sets the name shown in the language picker with an `X-Language-Name` header. A regular language does not need that header.

### Placeholders and markup

Keep these in the translation:

| In the text | What it is | Rule |
| --- | --- | --- |
| `{0}`, `{1}`, ... | Values filled in by SKIF: names, numbers, icons, logos, keys. | They may be moved around or left out, but no new ones may be added. A translation with a placeholder the English text does not have is ignored (and logged). |
| `[Library]` | A clickable link, or a highlighted word (the comment says which). | Translate the text inside, keep the brackets. A link's text should match the name of what it points to (e.g. the translated name of the tab). |
| `[[`, `]]` | A literal `[` or `]` in text that also has links. | Keep them doubled. |
| `\n` | A line break. | Keep or move; SKIF also wraps long lines on its own. |
| `%`, `%s`, `%d` | Plain characters. | Nothing special: SKIF never uses a translation as a printf format. |

Labels of buttons, menus and tabs are translated without their icons and without their internal ID (the part after `##`), so neither appears in the `.po` file.

### Contexts

Some English words need a different translation depending on where they are. Those entries have a context (`msgctxt`), shown in Poedit next to the text:

| Context | Where it is |
| --- | --- |
| `tab` | The names of the tabs (Library, Monitor, Hardware, Settings, About). |
| `category` | Built-in categories of the library (*Favorites*, *Games*). |
| `column` | Column headers of a table. |
| `key` | A key of the keyboard (*Escape*). |
| `gamepad button` | A button of a controller. |
| `update speed` | The refresh speeds of the Monitor tab. |
| `injection status` | The status of Special K in a running game (e.g. *Active*, *Inert*). |
| `platform` | The platform of a game (*Custom*: added by the user). |
| `cover scaling` | How cover art fills its frame. |
| `display` | Information about a monitor (e.g. an *Unknown* model). |
| `diagnostics` | The levels of diagnostics (*None*, *Normal*, *Enhanced*). |
| `log level` | The levels of the log (*Error*, *Warning*, ...). |

### Limits

* **Notification titles: at most 63 characters** (a Windows limit; longer titles are cut off). These entries say so in their comment.
* There are no other length limits: SKIF wraps text and widens or shortens its layouts as needed. Still, prefer the shorter of two equally good wordings for buttons, menu items and column headers.

### What stays in English on purpose

* The names of games, apps, stores (Steam, GOG, Epic, Xbox, Ubisoft, itch.io), Special K and SKIF, and Windows features where Special K shows them in English (e.g. presentation modes like *Hardware: Independent Flip*; the comments say so).
* The descriptions of settings in the INI editor (they come from Special K) and the content of the INI files themselves.
* Log files, file and folder names, command line arguments and registry keys.
* "32-bit" and "64-bit", the data rows of the *Developer* submenu, and the process details (tags) in the Monitor tab.
* The message boxes of the command-line admin actions, which run before the language is loaded.
* Text that comes from the internet: release notes and the news of updates.

### Testing a translation

No build of SKIF is needed:

1. Put the `.po` file in `<Special K folder>\Languages\` (create the folder if needed). It takes priority over an embedded file of the same language.
2. Restart SKIF (files are read at startup).
3. Pick the language in *Settings > Appearance > Language*. The picker shows how much of each language is translated.
4. Check `SKIF.log`: it lists every file found, how much of it is translated, syntax errors (with the line), and entries that were skipped (e.g. because of a placeholder that does not exist).

Things to look at: text that is cut off or overlaps, buttons that got too wide, and mini mode and horizontal mode with a narrow window.

### Sending a translation

Translations are contributed as pull requests to [SpecialKO/SKIF](https://github.com/SpecialKO/SKIF):

1. Fork the repository and create a branch (e.g. `l10n-pt-BR`).
2. Add or update `resources/l10n/<language>.po`. Only that file is needed: a maintainer embeds new languages in `l10n.rc`.
3. Update it from the current `SKIF.pot` first, so it covers the latest text (Poedit: *Translation > Update from POT file*).
4. Test it as described above, and mention in the pull request which parts of SKIF you checked.
5. Open the pull request with the language in the title, e.g. *Translation: Portuguese (Brazil)*.

A partial translation is welcome: untranslated text shows in English, and the picker shows how complete each language is. Improvements to an existing translation are sent the same way; if the language has a regular translator, they may be asked to review.

## Maintaining

### Routine

* After changing UI text, run `powershell -ExecutionPolicy Bypass -File tools\l10n_extract.ps1` to update `SKIF.pot`, and commit it with the change. Translators update their `.po` files from it in their next pull request; to do it for them, use Poedit (*Translation > Update from POT file*) or `msgmerge --update --backup=off <language>.po SKIF.pot` (GNU gettext).
* Reviewing a translation pull request: open the `.po` file in SKIF via the `Languages` folder and check `SKIF.log` for skipped entries and syntax errors. The `Language` and `Plural-Forms` headers must be set; the picker uses the language's own name from Windows.
* To embed a new language, add a line to `l10n.rc`: `de SKIF_PO "resources\\l10n\\de.po"`. Do this when a language is complete enough to ship; until then it can live in the repository without being embedded.
* The *Pseudo-locale* (listed in the language picker when *Settings > Advanced > Developer mode* is enabled) shows every translated text accented and ~35% longer, which makes text that is not marked for translation (it stays plain English) and layouts that are too narrow for longer languages easy to spot.
* Only the catalog of the active language is parsed; the others are only scanned at startup (for their tag, name and completion), and parsed when switched to.

### Marking text in the sources

The macros are described at the top of `include\utility\l10n.h`. In short:

| Macro | Use |
| --- | --- |
| `_("text")`, `C_("context", "text")` | Display text. |
| `L_`, `LI_`, `CL_` | Widget labels; the ID (after `###`, or the whole English label) stays the same in every language. |
| `W_`, `CW_` | Wide text for Win32 (MessageBox, tray menu, jump list, notifications). |
| `N_`/`T_`, `NC_`/`CT_` | Text in static arrays: marked where it is defined, translated where it is shown. |
| `NL_`/`TL_` | The same for widget labels (e.g. the buttons of prompts). |
| `P_` | Plural forms. |
| `F_`, `FP_` | Translate, then format with `{0}`, `{1}`, ... |

Rules:

* **Never pass a translation as a printf format.** `ImGui::TextWrapped ("%s", _("100% free"))`, not `ImGui::TextWrapped (_("100% free"))`.
* **Sliders and drags** take a printf format: build it with `SKIF_L10n_PrintfFormat (_("{0} nits"), "%d")`, which escapes any other `%`.
* **Translate once.** `F_` translates its pattern. To format text that is already translated (e.g. a message passed to a prompt), use `SKIF_L10n_Format (translated, english, args...)`, which falls back to the English pattern if the translation fails to format.
* **Keep sentences whole.** Use `SKIF_ImGui_TextRich` (clickable `[links]`), `SKIF_ImGui_TextRichHighlight` (highlighted `[words]`) or `SKIF_ImGui_TextHighlighted` (`{0}` in a color) instead of gluing pieces of a sentence together. Text put inside `[...]` at runtime must go through `SKIF_ImGui_TextRichEscape`.
* **Keep icons and macros out of the text.** Prefix icons go outside (`LI_`, or `ICON_FA_X " "` + text); icons within a sentence are passed as `{0}`.
* **Explain placeholders and limits** with a `// TRANSLATORS:` comment on the line before; the extractor copies it into `SKIF.pot`. Notification titles always get `(at most 63 characters)`.
* **Use a context** when the same English text needs different translations; reuse an existing one where it fits.
* **Sources stay ASCII.** The project is not built with `/utf-8`, so non-ASCII text goes in the `.po` files only.
* **Include `<utility/l10n.h>` last** in each source file: its short macro names (`_`, `L_`, `T_`, ...) would otherwise clash with other headers.
* **Measure, don't assume widths.** Size buttons and columns from `ImGui::CalcTextSize` of the translated text, and let text wrap or end in an ellipsis (`SKIF_ImGui_TextColoredEllipsis`) where space is fixed.

### The ImGui patch for CJK line breaks

`src\imgui\imgui_draw.cpp` carries a small SKIF patch that lets Chinese and Japanese text wrap between any two characters (these languages do not use spaces), with simple rules that keep closing punctuation and small kana off the start of a line and opening brackets off its end. Korean and Latin text wrap as before. It is wrapped in `#ifdef SKIF_L10N_CJK_LINE_BREAKS` blocks, enabled in `include\imgui\imconfig.h`, and exports `ImTextIsCJKBreakOpportunity ( )`, which `SKIF_ImGui_TextRich` uses too. When updating ImGui, re-apply the blocks marked `SKIF_L10N_CJK_LINE_BREAKS`.
