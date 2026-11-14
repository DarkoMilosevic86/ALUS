# Configuring ALUS

Everything the settings window changes is stored in plain ini files, which
you can also edit by hand. The format is GLib's key file format, the same
`[section]` / `key = value` format NVDA uses, so NVDA's `gestures.ini` files
work unchanged.

ALUS notices when `alus.ini` changes on disk and reloads it in place, without
a restart. The settings window writes the file once, when you press OK.

## Files

| Path | What |
|---|---|
| `~/.config/alus/alus.ini` | the main configuration |
| `~/.config/alus/gestures.ini` | your key bindings, layered over the built-in map |
| `~/.config/alus/symbols-<lang>.dic` | your own symbol names (e.g. `symbols-sr.dic`) |
| `~/.config/alus/dictionaries/default.dic` | your pronunciation dictionary |
| `~/.config/alus/dictionaries/<synth>-<voice>.dic` | a pronunciation dictionary for one voice |
| `~/.config/alus/profiles/<name>.ini` | configuration profiles (see below) |
| `<datadir>/alus/` | the installed data: command table, built-in gesture map, symbol and character dictionaries, per-language data |

The configuration directory is `$ALUS_CONFIG_DIR` if set, otherwise
`~/.config/alus` (`$XDG_CONFIG_HOME/alus`). `alus-daemon --config-dir=DIR`
overrides both for one run, which is handy for testing a clean
configuration.

## `alus.ini`

A setting that is not in the file has its default; the file only needs what
you changed. Values are case sensitive. Booleans are `true` / `false`.

Example:

```ini
[general]
language = sr

[speech]
outputModule = espeak-ng
voice = sr
rate = 55
symbolLevel = most

[keyboard]
keyboardLayout = laptop
alusKeyCapsLock = true
```

### `[general]`

| Key | Default | Meaning |
|---|---|---|
| `language` | (session) | the screen reader's language: empty for the session's, `en`, `sr`, `sr@latin` |
| `logLevel` | `info` | `off`, `critical`, `error`, `warning`, `info`, `debugwarning`, `io`, `debug` |

### `[speech]`

| Key | Default | Meaning |
|---|---|---|
| `synth` | `ssip` | the synthesizer driver (`ssip` = speech-dispatcher) |
| `outputModule` | (speech-dispatcher's default) | the speech-dispatcher module, e.g. `espeak-ng`, `rhvoice` |
| `voice` | (module default) | a voice id from `alus-cli list-voices` |
| `voiceVariant` | | an eSpeak variant |
| `rate`, `pitch`, `inflection` | `50` | 0–100 |
| `volume` | `100` | 0–100 |
| `symbolLevel` | `some` | `none`, `some`, `most`, `all`, `char` |
| `speechMode` | `talk` | `talk`, `beeps`, `off`, `onDemand` |
| `autoLanguageSwitching` | `true` | switch the voice language when the text declares one |
| `autoDialectSwitching` | `false` | follow the dialect (`en-GB` vs `en-US`) too |
| `unicodeNormalization` | `false` | normalise Unicode before speaking |
| `beepSpeechModePitch` | `10000` | the beep pitch in beeps mode, Hz |
| `indexMarkOffsetMs` | `-40` | how early index marks arrive relative to the audio (measured, N13) |
| `lostIndexGraceMs` | `300` | how long to wait for a lost index mark after the synthesizer says it is done (risk R14) |
| `stallTimeoutMs` | `10000` | absolute deadline for a synthesizer that says nothing |

### `[keyboard]`

| Key | Default | Meaning |
|---|---|---|
| `keyboardLayout` | `desktop` | `desktop` or `laptop` |
| `alusKeyInsert` | `true` | Insert is an ALUS key |
| `alusKeyNumpadInsert` | `true` | numpad Insert is an ALUS key |
| `alusKeyCapsLock` | `false` | Caps Lock is an ALUS key |
| `multiPressTimeout` | `500` | ms within which a second press counts as a double press |
| `speakTypedCharacters` | `editControls` | `off`, `editControls`, `always` |
| `speakTypedWords` | `off` | `off`, `editControls`, `always` |
| `speakCommandKeys` | `false` | speak keys that are not characters |
| `speechInterruptForCharacters` | `true` | typing a character stops speech |
| `speechInterruptForEnter` | `true` | Enter stops speech |
| `allowSkimReadingInSayAll` | `false` | movement keys skim instead of stopping say all |

### `[input]`

Advanced; you should not need these.

| Key | Default | Meaning |
|---|---|---|
| `backend` | `auto` | the keyboard backend |
| `appId` | (screen reader role name) | the D-Bus name prefix ALUS requests for the keyboard monitor; the compositor allows only the screen reader role (risk R1) |
| `callTimeout` | `2000` | ms deadline for every call to the compositor |

### Other sections

These are what the settings window's pages write; each page's help text
explains every option, and the window is the easiest way to change them.

| Section | Settings page | Keys |
|---|---|---|
| `[braille]` | Braille | `display`, `brlapiHost`, `translationTable`, `inputTable`, `expandAtCursor`, `unicodeNormalization`, `showCursor`, `cursorBlink`, `cursorBlinkRate`, `cursorShapeFocus`, `cursorShapeReview`, `showSelection`, `tetherTo`, `mode`, `reviewRoutingMovesSystemCaret`, `focusContextPresentation`, `textWrap`, `readByParagraph`, `autoScrollRate`, `showMessages`, `messageTimeout`, `speakOnRouting`, `speakOnNavigatingByUnit`, `handleBrailleInput` |
| `[presentation]` | Object presentation | `reportObjectDescriptions`, `reportKeyboardShortcuts`, `reportObjectPositionInformation`, `reportTooltips`, `reportHelpBalloons`, `reportMultiSelect`, `reportDynamicContentChanges` |
| `[annotations]` | Object presentation | `reportDetails`, `reportAriaDescription` |
| `[objectNavigation]` | Review cursor | `reportEdge` |
| `[reviewCursor]` | Review cursor | `followFocus`, `followCaret`, `followMouse`, `simpleReviewMode` |
| `[documentNavigation]` | Document formatting | `paragraphStyle` (`application`, `single`, `multi`), `sayAllReadingUnit` (`paragraph`, `line`) |
| `[documentFormatting]` | Document formatting | font: `reportFontName`, `reportFontSize`, `reportFontAttributes`, `reportSuperscriptsAndSubscripts`, `reportEmphasis`, `reportHighlight`, `reportStyle`, `reportColor`; review: `reportComments`, `reportBookmarks`, `reportRevisions`, `reportSpellingErrors`, `reportGrammarErrors`; layout: `reportPage`, `reportLineNumber`, `reportLineIndentation`, `indentToneDuration`, `ignoreBlankLinesForRLI`, `reportLineSpacing`, `reportAlignment`; tables: `reportTables`, `reportTableCellCoords`, `reportTableHeaders`, `reportCellBorders`, `includeLayoutTables`; structure: `reportHeadings`, `reportLinks`, `reportLinkType`, `reportGraphics`, `reportLists`, `reportBlockQuotes`, `reportGroupings`, `reportLandmarks`, `reportArticles`, `reportFrames`, `reportFigures`, `reportClickable`; `detectFormatAfterCursor`, `reportFontAttributesInBraille`, `reportSpellingErrorsInBraille` |

The authoritative list, with every choice and its label, is
`src/gui/model.cpp`.

## `gestures.ini`

Your bindings are layered over the built-in map. A section names the object
that owns the commands, and each line binds a command to one or more
gestures, separated by commas:

```ini
[globalCommands.GlobalCommands]
; a second key for the time
dateTime = kb:alus+f12, kb:alus+shift+t
; unbind a key
None = kb:alus+f12

[browseMode.BrowseModeTreeInterceptor]
nextHeading = kb:h, kb:j
```

- Gestures are written `source:keys`: `kb:alus+shift+t`,
  `kb(laptop):alus+a` (laptop layout only), `kb(desktop):…`,
  `br(brltty):…` for a braille display, `bk:dot7` for braille input.
- Key order does not matter (`alus+shift+t` = `shift+alus+t`); a literal plus
  is `plus`.
- `None = <gesture>` unbinds the gesture.
- A file from NVDA works unchanged; its reader key spelling is accepted as
  `alus`.

Section names and command names are in the [command
reference](commands.md) and in `data/scripts.csv`. The gestures dialog
(Settings → Gestures) writes this file for you.

Layers, highest priority first: your `gestures.ini`, the language's
`<datadir>/alus/locale/<lang>/gestures.ini` (for example, the Serbian one
moves some laptop commands to `š` and `đ`), then the built-in
`<datadir>/alus/gestures-builtin.ini`.

## Symbol dictionaries

`symbols-<lang>.dic` uses NVDA's `symbols.dic` format. Your file is applied
after the built-in one and may override any symbol. Only the built-in file may
use regular-expression (complex) symbols.

```
symbols:
# symbol	replacement	level	preserve
→	strelica desno	some
```

## Pronunciation dictionaries

`dictionaries/default.dic` applies to every voice,
`dictionaries/<synth>-<voice>.dic` to one voice. They are applied before
symbols, in the order built-in → default → voice. Entries use NVDA's
`speechDicts` format: pattern, replacement, case sensitivity, and type
(anywhere, whole word, regular expression…). A whole-word pattern treats
letters with diacritics and combining marks as part of the word.

## Profiles

A profile is an ini file in `~/.config/alus/profiles/` that carries only the
settings it changes, layered over `alus.ini`; the most recently activated
profile wins. `[general]` can never be overridden by a profile. The profile
machinery and app-switch transactions are in place, but there is no dialog
for creating, activating or triggering profiles yet; the profile commands say
so when pressed.

## Environment variables

| Variable | Effect |
|---|---|
| `ALUS_CONFIG_DIR` | the configuration directory |
| `ALUS_DATA_DIR` | the data directory (before the installed one and `./data`) |
| `ALUS_LOCALE_DIR` | where the `.mo` translation catalogues are |
| `ALUS_LOUIS_TABLES_DIR` | the liblouis table directory |
| `ALUS_XKB_LAYOUT` | the keyboard layout used to map keys to characters, overriding the session's |
| `ALUS_REGEX_NO_JIT` | turn off the PCRE2 JIT (used under Valgrind) |
