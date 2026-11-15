# ALUS command reference

> **GENERATED** from `data/scripts.csv` by `tools/generate-command-reference.py`.
> Do not edit by hand: changes are lost at the next generation, and the tests
> (`meson test -C build --suite external --suite gui`) fail when this file and the command table disagree.

`ALUS` is the ALUS key: Insert, numpad Insert and/or Caps Lock, as set in
Settings → Keyboard. **Desktop** and **Laptop** are the two keyboard layouts.
A command without a key is in the table but not bound; bind it in the
gestures dialog (Settings → Gestures) or in `~/.config/alus/gestures.ini`.

Commands: **316** in 17 categories, **178** of them bound to keys.

**15** are not in this version and are marked so in the *Description* column: **15** have a key that **says** the command is missing, and **0** are deliberately left unbound because the key belongs to the application (N18). The source is `data/commands-status.ini`.

## Audio

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `cycleAudioDuckingMode` | ALUS+shift+d | ALUS+shift+d | **NOT IN THIS VERSION** (the key says so when pressed) — Cycles through audio ducking modes which determine when ALUS lowers the volume of other sounds |
| `cycleSoundSplit` | ALUS+alt+s | ALUS+alt+s | **NOT IN THIS VERSION** (the key says so when pressed) — Cycles through sound split modes |

## Braille

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `braille_cycleUnicodeNormalization` | — | — | Cycle through the braille Unicode normalization states |
| `braille_cycleCursorShape` | — | — | Cycle through the braille cursor shapes |
| `braille_cycleReviewRoutingMovesSystemCaret` | — | — | Cycle through the braille move system caret when routing review cursor states |
| `braille_cycleShowMessages` | — | — | Cycle through the braille show messages modes |
| `braille_cycleShowSelection` | — | — | Cycle through the braille show selection states |
| `decreaseBrailleAutoScrollRate` | ALUS+alt+j | ALUS+alt+j | Decreases the rate for braille automatic scroll |
| `braille_eraseLastCell` | bk:dot7 | bk:dot7 | Erases the last entered braille cell or character |
| `increaseBrailleAutoScrollRate` | ALUS+alt+l | ALUS+alt+l | Increases the rate for braille automatic scroll |
| `braille_dots` | bk:dots | bk:dots | Inputs braille dots via the braille keyboard |
| `braille_toFocus` | — | — | Moves the braille display to the current focus |
| `braille_nextLine` | — | — | Moves the braille display to the next line |
| `braille_previousLine` | — | — | Moves the braille display to the previous line |
| `braille_reportFormatting` | — | — | Reports formatting info for the text under this braille cell |
| `braille_routeTo` | — | — | Routes the cursor to or activates the object under this braille cell |
| `braille_scrollBack` | — | — | Scrolls the braille display back |
| `braille_scrollForward` | — | — | Scrolls the braille display forward |
| `braille_selectRange` | — | — | Selects the text from the first up to the last braille cell |
| `braille_toggleTether` | ALUS+control+t | ALUS+control+t | Toggle tethering of braille between the focus and the review position |
| `braille_toggleShowCursor` | — | — | Toggle the braille cursor on and off |
| `braille_toggleFocusContextPresentation` | — | — | Toggle the way context information is presented in braille |
| `toggleBrailleAutoScroll` | ALUS+alt+k | ALUS+alt+k | Toggles braille automatic scroll |
| `toggleBrailleMode` | ALUS+alt+t | ALUS+alt+t | Toggles braille mode |
| `toggleSpeakingOnNavigatingByUnit` | — | — | Toggles on and off speaking when navigating by lines or paragraph with braille |
| `braille_toggleSpeakOnRouting` | — | — | Toggles speaking the character under the cursor when routing cursor in text |
| `braille_translate` | bk:dot7+dot8 | bk:dot7+dot8 | Translates any braille input |
| `braille_enter` | bk:dot8 | bk:dot8 | Translates any braille input and presses the enter key |

## Browse mode

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `nextSelectedElement` | — | — | Moves to the next element of the selected type in browse mode touch navigation |
| `nextHeading1` | 1 | 1 | Moves to the next heading at level 1 |
| `nextHeading2` | 2 | 2 | Moves to the next heading at level 2 |
| `nextHeading3` | 3 | 3 | Moves to the next heading at level 3 |
| `nextHeading4` | 4 | 4 | Moves to the next heading at level 4 |
| `nextHeading5` | 5 | 5 | Moves to the next heading at level 5 |
| `nextHeading6` | 6 | 6 | Moves to the next heading at level 6 |
| `nextHeading7` | 7 | 7 | Moves to the next heading at level 7 |
| `nextHeading8` | 8 | 8 | Moves to the next heading at level 8 |
| `nextHeading9` | 9 | 9 | Moves to the next heading at level 9 |
| `prevSelectedElement` | — | — | Moves to the previous element of the selected type in browse mode touch navigation |
| `previousHeading1` | shift+1 | shift+1 | Moves to the previous heading at level 1 |
| `previousHeading2` | shift+2 | shift+2 | Moves to the previous heading at level 2 |
| `previousHeading3` | shift+3 | shift+3 | Moves to the previous heading at level 3 |
| `previousHeading4` | shift+4 | shift+4 | Moves to the previous heading at level 4 |
| `previousHeading5` | shift+5 | shift+5 | Moves to the previous heading at level 5 |
| `previousHeading6` | shift+6 | shift+6 | Moves to the previous heading at level 6 |
| `previousHeading7` | shift+7 | shift+7 | Moves to the previous heading at level 7 |
| `previousHeading8` | shift+8 | shift+8 | Moves to the previous heading at level 8 |
| `previousHeading9` | shift+9 | shift+9 | Moves to the previous heading at level 9 |
| `toggleVirtualBufferPassThrough` | ALUS+space | ALUS+space | Toggles between browse mode and focus mode. When in focus mode, keys will pass straight through to the application, allowing you to interact directly with a control. When in browse mode, you can navigate the document with the cursor, quick navigation keys, etc. |
| `toggleNativeAppSelectionMode` | ALUS+shift+f10 | ALUS+shift+f10 | **NOT IN THIS VERSION** (the key says so when pressed) — Toggles native selection mode on and off |
| `toggleScreenLayout` | ALUS+v | ALUS+v | Toggles on and off if the screen layout is preserved while rendering the document content |
| `findNext` | ALUS+f3 | ALUS+f3 | find the next occurrence of the previously entered text string from the current cursor's position |
| `findPrevious` | ALUS+shift+f3 | ALUS+shift+f3 | find the previous occurrence of the previously entered text string from the current cursor's position |
| `nextAnnotation` | a | a | moves to the next annotation |
| `nextArticle` | — | — | moves to the next article |
| `nextBlockQuote` | q | q | moves to the next block quote |
| `nextButton` | b | b | moves to the next button |
| `nextCheckBox` | x | x | moves to the next check box |
| `nextClickable` | — | — | moves to the next clickable element |
| `nextComboBox` | c | c | moves to the next combo box |
| `nextDifferentStyle` | — | — | moves to the next different style text |
| `nextEdit` | e | e | moves to the next edit field |
| `nextEmbeddedObject` | o | o | moves to the next embedded object |
| `nextError` | w | w | moves to the next error |
| `nextFigure` | — | — | moves to the next figure |
| `nextFormField` | f | f | moves to the next form field |
| `nextFrame` | m | m | moves to the next frame |
| `nextGraphic` | g | g | moves to the next graphic |
| `nextGrouping` | — | — | moves to the next grouping |
| `nextHeading` | h | h | moves to the next heading |
| `nextLandmark` | d | d | moves to the next landmark |
| `nextLink` | k | k | moves to the next link |
| `nextList` | l | l | moves to the next list |
| `nextListItem` | i | i | moves to the next list item |
| `nextMenuItem` | — | — | moves to the next menu item |
| `nextProgressBar` | — | — | moves to the next progress bar |
| `nextRadioButton` | r | r | moves to the next radio button |
| `nextReference` | — | — | moves to the next reference |
| `nextSameStyle` | — | — | moves to the next same style text |
| `nextSeparator` | s | s | moves to the next separator |
| `nextSlider` | — | — | moves to the next slider |
| `nextTab` | — | — | moves to the next tab |
| `nextTable` | t | t | moves to the next table |
| `nextTextParagraph` | p | p | moves to the next text paragraph |
| `nextToggleButton` | — | — | moves to the next toggle button |
| `nextUnvisitedLink` | u | u | moves to the next unvisited link |
| `nextVerticalParagraph` | — | — | moves to the next vertically aligned paragraph |
| `nextVisitedLink` | v | v | moves to the next visited link |
| `previousAnnotation` | shift+a | shift+a | moves to the previous annotation |
| `previousArticle` | — | — | moves to the previous article |
| `previousBlockQuote` | shift+q | shift+q | moves to the previous block quote |
| `previousButton` | shift+b | shift+b | moves to the previous button |
| `previousCheckBox` | shift+x | shift+x | moves to the previous check box |
| `previousClickable` | — | — | moves to the previous clickable element |
| `previousComboBox` | shift+c | shift+c | moves to the previous combo box |
| `previousDifferentStyle` | — | — | moves to the previous different style text |
| `previousEdit` | shift+e | shift+e | moves to the previous edit field |
| `previousEmbeddedObject` | shift+o | shift+o | moves to the previous embedded object |
| `previousError` | shift+w | shift+w | moves to the previous error |
| `previousFigure` | — | — | moves to the previous figure |
| `previousFormField` | shift+f | shift+f | moves to the previous form field |
| `previousFrame` | shift+m | shift+m | moves to the previous frame |
| `previousGraphic` | shift+g | shift+g | moves to the previous graphic |
| `previousGrouping` | — | — | moves to the previous grouping |
| `previousHeading` | shift+h | shift+h | moves to the previous heading |
| `previousLandmark` | shift+d | shift+d | moves to the previous landmark |
| `previousLink` | shift+k | shift+k | moves to the previous link |
| `previousList` | shift+l | shift+l | moves to the previous list |
| `previousListItem` | shift+i | shift+i | moves to the previous list item |
| `previousMenuItem` | — | — | moves to the previous menu item |
| `previousProgressBar` | — | — | moves to the previous progress bar |
| `previousRadioButton` | shift+r | shift+r | moves to the previous radio button |
| `previousReference` | — | — | moves to the previous reference |
| `previousSameStyle` | — | — | moves to the previous same style text |
| `previousSeparator` | shift+s | shift+s | moves to the previous separator |
| `previousSlider` | — | — | moves to the previous slider |
| `previousTab` | — | — | moves to the previous tab |
| `previousTable` | shift+t | shift+t | moves to the previous table |
| `previousTextParagraph` | shift+p | shift+p | moves to the previous text paragraph |
| `previousToggleButton` | — | — | moves to the previous toggle button |
| `previousUnvisitedLink` | shift+u | shift+u | moves to the previous unvisited link |
| `previousVerticalParagraph` | — | — | moves to the previous vertically aligned paragraph |
| `previousVisitedLink` | shift+v | shift+v | moves to the previous visited link |
| `previousNotLinkBlock` | shift+n | shift+n | skips backward past a block of links |
| `nextNotLinkBlock` | n | n | skips forward past a block of links |

## Configuration

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `revertConfiguration` | ALUS+control+r | ALUS+control+r | Pressing once reverts the current configuration to the most recently saved state. Pressing three times resets to factory defaults. |
| `reportActiveConfigurationProfile` | — | — | Reports the name of the current ALUS configuration profile |
| `saveConfiguration` | ALUS+control+c | ALUS+control+c | Saves the current ALUS configuration |
| `activateAddonStoreSettings` | — | — | Shows ALUS's Add-on Store settings |
| `activateAdvancedSettings` | — | — | Shows ALUS's Advanced settings |
| `activateAudioSettingsDialog` | ALUS+control+u | ALUS+control+u | Shows ALUS's audio settings |
| `activateBrailleSettingsDialog` | — | — | Shows ALUS's braille settings |
| `activateBrowseModeDialog` | ALUS+control+b | ALUS+control+b | Shows ALUS's browse mode settings |
| `activateDocumentFormattingDialog` | ALUS+control+d | ALUS+control+d | Shows ALUS's document formatting settings |
| `activateGeneralSettingsDialog` | ALUS+control+g | ALUS+control+g | Shows ALUS's general settings |
| `activateInputCompositionDialog` | — | — | Shows ALUS's input composition settings |
| `activateKeyboardSettingsDialog` | ALUS+control+k | ALUS+control+k | Shows ALUS's keyboard settings |
| `activateMouseSettingsDialog` | ALUS+control+m | ALUS+control+m | Shows ALUS's mouse settings |
| `activateObjectPresentationDialog` | ALUS+control+o | ALUS+control+o | Shows ALUS's object presentation settings |
| `activatePrivacyAndSecuritySettings` | — | — | Shows ALUS's privacy and security settings |
| `activateReviewCursorDialog` | — | — | Shows ALUS's review cursor settings |
| `activateVoiceDialog` | ALUS+control+v | ALUS+control+v | Shows ALUS's speech settings |
| `activateBrailleDisplayDialog` | ALUS+control+a | ALUS+control+a | Shows the ALUS braille display selection dialog |
| `activateDefaultDictionaryDialog` | — | — | Shows the ALUS default dictionary dialog |
| `activateInputGesturesDialog` | — | — | Shows the ALUS input gestures dialog |
| `activateSpeechSymbolsDialog` | — | — | Shows the ALUS symbol pronunciation dialog |
| `activateSynthesizerDialog` | ALUS+control+s | ALUS+control+s | Shows the ALUS synthesizer selection dialog |
| `activateTemporaryDictionaryDialog` | — | — | Shows the ALUS temporary dictionary dialog |
| `activateVoiceDictionaryDialog` | — | — | Shows the ALUS voice-specific dictionary dialog |
| `toggleConfigProfileTriggers` | — | — | Toggles disabling of all configuration profile triggers. Disabling remains in effect until ALUS is restarted |

## Configuration profiles

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `activateConfigProfilesDialog` | ALUS+control+p | ALUS+control+p | Shows the ALUS Configuration Profiles dialog |

## Document formatting

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `toggleReportTableHeaders` | — | — | Cycle through the possible modes to report table row and column headers |
| `toggleReportFontAttributes` | — | — | Cycles font attribute reporting between speech, braille, speech and braille, and off. |
| `toggleReportLineIndentation` | — | — | Cycles through line indentation settings |
| `toggleReportSpellingErrors` | — | — | Cycles through options for how to report spelling or grammar errors |
| `toggleReportCellBorders` | — | — | Cycles through the cell border reporting settings |
| `toggleReportClickable` | — | — | Toggles on and off reporting if clickable |
| `toggleignoreBlankLinesForReportLineIndentation` | — | — | Toggles on and off the ignoring of blank lines for line indentation reporting |
| `toggleReportArticles` | — | — | Toggles on and off the reporting of articles |
| `toggleReportBlockQuotes` | — | — | Toggles on and off the reporting of block quotes |
| `toggleReportColor` | — | — | Toggles on and off the reporting of colors |
| `toggleReportComments` | — | — | Toggles on and off the reporting of comments |
| `toggleReportEmphasis` | — | — | Toggles on and off the reporting of emphasis |
| `toggleReportFigures` | — | — | Toggles on and off the reporting of figures and captions |
| `toggleReportFontName` | — | — | Toggles on and off the reporting of font changes |
| `toggleReportFontSize` | — | — | Toggles on and off the reporting of font size changes |
| `toggleReportFrames` | — | — | Toggles on and off the reporting of frames |
| `toggleReportGraphics` | — | — | Toggles on and off the reporting of graphics |
| `toggleReportGroupings` | — | — | Toggles on and off the reporting of groupings |
| `toggleReportHeadings` | — | — | Toggles on and off the reporting of headings |
| `toggleReportHighlightedText` | — | — | Toggles on and off the reporting of highlighted text |
| `toggleReportLandmarks` | — | — | Toggles on and off the reporting of landmarks |
| `toggleReportLineNumber` | — | — | Toggles on and off the reporting of line numbers |
| `toggleReportLineSpacing` | — | — | Toggles on and off the reporting of line spacing |
| `toggleReportLinkType` | — | — | Toggles on and off the reporting of link type |
| `toggleReportLinks` | — | — | Toggles on and off the reporting of links |
| `toggleReportLists` | — | — | Toggles on and off the reporting of lists |
| `toggleReportPage` | — | — | Toggles on and off the reporting of pages |
| `toggleReportParagraphIndentation` | — | — | Toggles on and off the reporting of paragraph indentation |
| `toggleReportRevisions` | — | — | Toggles on and off the reporting of revisions |
| `toggleReportStyle` | — | — | Toggles on and off the reporting of style changes |
| `toggleReportSuperscriptsAndSubscripts` | — | — | Toggles on and off the reporting of superscripts and subscripts |
| `toggleReportTableCellCoords` | — | — | Toggles on and off the reporting of table cell coordinates |
| `toggleReportTables` | — | — | Toggles on and off the reporting of tables |
| `toggleReportAlignment` | — | — | Toggles on and off the reporting of text alignment |
| `toggleReportSpellingErrorsInBraille` | — | — | Toggles reporting spelling or grammar errors in braille |

## Emulated system keyboard keys

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `braille_toggleAlusKeyShift` | — | — | Virtually toggles the ALUS and shift keys to emulate a keyboard shortcut with braille input |
| `braille_toggleAlusKey` | — | — | Virtually toggles the ALUS key to emulate a keyboard shortcut with braille input |
| `braille_toggleAltShift` | — | — | Virtually toggles the alt and shift keys to emulate a keyboard shortcut with braille input |
| `braille_toggleAlt` | — | — | Virtually toggles the alt key to emulate a keyboard shortcut with braille input |
| `braille_toggleControlAlt` | — | — | Virtually toggles the control and alt keys to emulate a keyboard shortcut with braille input |
| `braille_toggleControlShift` | — | — | Virtually toggles the control and shift keys to emulate a keyboard shortcut with braille input |
| `braille_toggleControl` | — | — | Virtually toggles the control key to emulate a keyboard shortcut with braille input |
| `braille_toggleControlAltShift` | — | — | Virtually toggles the control, alt, and shift keys to emulate a keyboard shortcut with braille input |
| `braille_toggleWindowsShift` | — | — | Virtually toggles the left windows and shift keys to emulate a keyboard shortcut with braille input |
| `braille_toggleWindows` | — | — | Virtually toggles the left windows key to emulate a keyboard shortcut with braille input |
| `braille_toggleShift` | — | — | Virtually toggles the shift key to emulate a keyboard shortcut with braille input |

## Input

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `passNextKeyThrough` | ALUS+f2 | ALUS+f2 | The next key that is pressed will not be handled at all by ALUS, it will be passed directly through to the application. |
| `toggleKeyboardLayout` | — | — | Toggles between desktop and laptop keyboard layout |
| `toggleInputHelp` | ALUS+1 | ALUS+1 | Turns input help on or off. When on, any input such as pressing a key on the keyboard will tell you what script is associated with that input, if any. |

## Miscellaneous

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `quit` | ALUS+q | ALUS+q | Quits ALUS! |
| `restart` | — | — | Restarts ALUS! |
| `showGui` | ALUS+a | ALUS+n | Shows the ALUS menu |
| `toggleCurrentAppSleepMode` | ALUS+shift+s | ALUS+shift+z | Toggles sleep mode on and off for the active application. |

## Mouse

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `leftMouseClick` | numpadDivide | numpadDivide, ALUS+[ | **NOT IN THIS VERSION** (the key says so when pressed) — Clicks the left mouse button once at the current mouse position |
| `rightMouseClick` | numpadMultiply | numpadMultiply, ALUS+] | **NOT IN THIS VERSION** (the key says so when pressed) — Clicks the right mouse button once at the current mouse position |
| `toggleLeftMouseButton` | shift+numpadDivide | shift+numpadDivide, ALUS+control+[ | **NOT IN THIS VERSION** (the key says so when pressed) — Locks or unlocks the left mouse button |
| `toggleRightMouseButton` | shift+numpadMultiply | shift+numpadMultiply, ALUS+control+] | **NOT IN THIS VERSION** (the key says so when pressed) — Locks or unlocks the right mouse button |
| `moveMouseToNavigatorObject` | ALUS+numpadDivide | ALUS+numpadDivide, ALUS+shift+m | **NOT IN THIS VERSION** (the key says so when pressed) — Moves the mouse pointer to the current navigator object |
| `mouseScrollDown` | — | — | Scroll down at the mouse position |
| `mouseScrollLeft` | — | — | Scroll left at the mouse position |
| `mouseScrollRight` | — | — | Scroll right at the mouse position |
| `mouseScrollUp` | — | — | Scroll up at the mouse position |
| `moveNavigatorObjectToMouse` | ALUS+numpadMultiply | ALUS+numpadMultiply, ALUS+shift+n | **NOT IN THIS VERSION** (the key says so when pressed) — Sets the navigator object to the current object under the mouse pointer and speaks it |
| `toggleMouseAudioCoordinates` | — | — | Toggles beeps that report mouse coordinates as the mouse moves |
| `toggleMouseTextResolution` | — | — | Toggles how much text will be spoken when the mouse moves |
| `toggleMouseTracking` | ALUS+m | ALUS+m | **NOT IN THIS VERSION** (the key says so when pressed) — Toggles the reporting of information as the mouse moves |

## Object navigation

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `navigatorObject_firstChild` | ALUS+numpad2 | ALUS+numpad2, ALUS+shift+downArrow | Moves the navigator object to the first object inside it |
| `navigatorObject_next` | ALUS+numpad6 | ALUS+numpad6, ALUS+shift+rightArrow | Moves the navigator object to the next object |
| `navigatorObject_parent` | ALUS+numpad8 | ALUS+numpad8, ALUS+shift+upArrow | Moves the navigator object to the object containing it |
| `navigatorObject_previous` | ALUS+numpad4 | ALUS+numpad4, ALUS+shift+leftArrow | Moves the navigator object to the previous object |
| `navigatorObject_nextInFlow` | ALUS+numpad3 | ALUS+numpad3, shift+ALUS+] | Moves to the next object in a flattened view of the object navigation hierarchy |
| `navigatorObject_previousInFlow` | ALUS+numpad9 | ALUS+numpad9, shift+ALUS+[ | Moves to the previous object in a flattened view of the object navigation hierarchy |
| `review_activate` | ALUS+numpadEnter | ALUS+numpadEnter, ALUS+enter | Performs the default action on the current navigator object (example: presses it if it is a button). |
| `navigatorObject_moveFocus` | ALUS+shift+numpadMinus | ALUS+shift+numpadMinus, ALUS+shift+backspace | Pressed once sets the keyboard focus to the navigator object, pressed twice sets the system caret to the position of the review cursor |
| `reviewCursorToStatusLine` | — | — | Reads the current application status bar and moves navigator object into it. |
| `reportCurrentNavigatorObjectLocation` | — | — | Reports information about the location of the current navigator object. |
| `reportReviewCursorLocation` | — | — | Reports information about the location of the text at the review cursor, or location of the navigator object if there is no text under review cursor. |
| `navigatorObject_currentDimensions` | ALUS+shift+numpadDelete | ALUS+shift+numpadDelete, ALUS+shift+delete | Reports information about the location of the text or object at the review cursor. Pressing twice may provide further detail. |
| `navigatorObject_current` | ALUS+numpad5 | ALUS+numpad5, ALUS+shift+o | Reports the current navigator object. Pressing twice spells this information, and pressing three times Copies name and value of this object to the clipboard |
| `navigatorObject_toFocus` | ALUS+numpadMinus | ALUS+numpadMinus, ALUS+backspace | Sets the navigator object to the current focus, and the review cursor to the position of the caret inside it, if possible. |
| `toggleFocusMovesNavigatorObject` | ALUS+7 | ALUS+7 | Toggles on and off the movement of the navigator object due to focus changes |
| `toggleSimpleReviewMode` | — | — | Toggles simple review mode on and off |

## Speech

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `copyLastSpokenInformation` | ALUS+control+x | ALUS+control+x | **NOT IN THIS VERSION** (the key says so when pressed) — Copies the last spoken information to the clipboard. |
| `speech_cycleUnicodeNormalization` | — | — | Cycle through the speech Unicode normalization states |
| `speechMode` | ALUS+s | ALUS+s | Cycles between speech modes. |
| `toggleSpeakTypedCharacters` | ALUS+2 | ALUS+2 | Cycles through options for when to speak typed characters. |
| `toggleSpeakTypedWords` | ALUS+3 | ALUS+3 | Cycles through options for when to speak typed words. |
| `cycleSpeechSymbolLevel` | ALUS+p | ALUS+p | Cycles through speech symbol levels which determine what symbols are spoken |
| `cycleSpeechAutomaticLanguageSwitching` | — | — | Cycles through the possible choices for automatic language switching: off, language only and language and dialect. |
| `decreaseSynthSetting` | ALUS+control+downArrow | ALUS+control+shift+downArrow | Decreases the currently active setting in the synth settings ring |
| `decreaseLargeSynthSetting` | ALUS+control+pageDown | ALUS+control+shift+pageDown | Decreases the currently active setting in the synth settings ring in a larger step |
| `increaseSynthSetting` | ALUS+control+upArrow | ALUS+shift+control+upArrow | Increases the currently active setting in the synth settings ring |
| `increaseLargeSynthSetting` | ALUS+control+pageUp | ALUS+shift+control+pageUp | Increases the currently active setting in the synth settings ring in a larger step |
| `nextSynthSetting` | ALUS+control+rightArrow | ALUS+shift+control+rightArrow | Moves to the next available setting in the synth settings ring |
| `previousSynthSetting` | ALUS+control+leftArrow | ALUS+shift+control+leftArrow | Moves to the previous available setting in the synth settings ring |
| `repeatLastSpokenInformation` | ALUS+x | ALUS+x | Repeat the last spoken information. Pressing twice shows it in a browsable message. |
| `firstValueSynthRing` | — | — | Set the first value of the current setting in the synth settings ring |
| `lastValueSynthRing` | — | — | Set the last value of the current setting in the synth settings ring |
| `toggleProgressBarOutput` | ALUS+u | ALUS+u | Toggles between beeps, speech, beeps and speech, and off, for reporting progress bar updates |
| `toggleDelayedCharacterDescriptions` | — | — | Toggles on and off delayed descriptions for characters on cursor movement |
| `toggleReportCLDR` | — | — | Toggles on and off the reporting of CLDR characters, such as emojis |
| `toggleReportDynamicContentChanges` | ALUS+5 | ALUS+5 | Toggles on and off the reporting of dynamic content changes, such as new text in dos console windows |
| `toggleSpeakCommandKeys` | ALUS+4 | ALUS+4 | Toggles on and off the speaking of command keys |

## System caret

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `reportCurrentSelection` | ALUS+shift+upArrow | ALUS+shift+s | Announces the current selection in edit controls and documents. Pressing twice spells this information. Pressing three times spells it using character descriptions. Pressing four times shows it in a browsable message. |
| `cycleParagraphStyle` | — | — | Cycles through paragraph navigation styles |
| `showFormattingAtCaret` | — | — | Presents, in browse mode, formatting info for the text under the caret. |
| `sayAll` | ALUS+downArrow | ALUS+a | Reads from the system caret up to the end of the text, moving the caret as it goes |
| `reportDetailsSummary` | ALUS+d | ALUS+d | Report summary of any annotation details at the system caret. |
| `reportFormattingAtCaret` | — | — | Reports formatting info for the text under the caret. |
| `reportOrShowFormattingAtCaret` | ALUS+f | ALUS+f | Reports formatting info for the text under the caret. If pressed twice, presents the information in browse mode |
| `reportCaretLocation` | — | — | Reports information about the location of the text at the caret, or location of the currently focused object if there is no caret. |
| `caretPos_currentDimensions` | ALUS+numpadDelete | ALUS+numpadDelete, ALUS+delete | Reports information about the location of the text or object at the position of system caret. Pressing twice may provide further detail. |
| `reportCurrentLine` | ALUS+upArrow | ALUS+l | Reports the current line under the application cursor. Pressing this key twice will spell the current line. Pressing three times will spell the line using character descriptions. |
| `reportCaretLanguage` | — | — | Reports the language for the text under the caret. If pressed twice, presents the information in browse mode |

## System focus

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `copyStatusLine` | — | — | Copies content of the status bar of current application to the clipboard. |
| `moveToParentTreeInterceptor` | ALUS+control+space | ALUS+control+space | Moves the focus out of the current embedded object and into the document that contains it |
| `speakForeground` | ALUS+b | ALUS+b | Reads all controls in the active window |
| `readStatusLine` | — | — | Reads the current application status bar. |
| `reportStatusLine` | ALUS+end | ALUS+shift+end | Reads the current application status bar. If pressed twice, spells the information. If pressed three times, copies the status bar to the clipboard |
| `reportFocusObjectLocation` | — | — | Reports information about the location of the currently focused object. |
| `reportCurrentFocus` | ALUS+tab | ALUS+tab | Reports the object with focus. If pressed twice, spells the information. Pressing three times spells it using character descriptions. |
| `reportFocusObjectAccelerator` | shift+numpad2 | shift+numpad2, ALUS+control+shift+. | Reports the shortcut key of the currently focused object |
| `title` | ALUS+t | ALUS+t | Reports the title of the current application or foreground window. If pressed twice, spells the title. If pressed three times, copies the title to the clipboard |
| `spellStatusLine` | — | — | Spells the current application status bar. |

## System status

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `dateTime` | ALUS+f12 | ALUS+f12 | If pressed once, reports the current time. If pressed twice, reports the current date |
| `say_battery_status` | ALUS+shift+b | ALUS+shift+b | Reports battery status and time remaining if AC is not plugged in |
| `reportClipboardText` | ALUS+c | ALUS+c | **NOT IN THIS VERSION** (the key says so when pressed) — Reports the text on the system clipboard. Pressing twice spells this information. Pressing three times spells it using character descriptions. |

## Text review

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `review_copy` | ALUS+f10 | ALUS+f10 | If pressed once, the text from the previously set start marker up to and including the current position of the review cursor is selected. If pressed twice, the text is copied to the clipboard |
| `review_markStartForCopy` | ALUS+f9 | ALUS+f9 | Marks the current position of the review cursor as the start of text to be selected or copied |
| `review_moveToStartMarkedForCopy` | ALUS+shift+F9 | ALUS+shift+F9 | Move the review cursor to the position marked as the start of text to be selected or copied |
| `review_bottom` | shift+numpad9 | shift+numpad9, ALUS+control+end | Moves the review cursor to the bottom line of the current navigator object and speaks it |
| `review_startOfLine` | shift+numpad1 | shift+numpad1, ALUS+home | Moves the review cursor to the first character of the line where it is situated in the current navigator object and speaks it |
| `review_startOfSelection` | ALUS+alt+home | ALUS+alt+home | Moves the review cursor to the first character of the selection, and speaks it |
| `review_endOfLine` | shift+numpad3 | shift+numpad3, ALUS+end | Moves the review cursor to the last character of the line where it is situated in the current navigator object and speaks it |
| `review_endOfSelection` | ALUS+alt+end | ALUS+alt+end | Moves the review cursor to the last character of the selection, and speaks it |
| `review_nextCharacter` | numpad3 | numpad3, ALUS+rightArrow | Moves the review cursor to the next character of the current navigator object and speaks it |
| `review_nextLine` | numpad9 | numpad9, ALUS+downArrow | Moves the review cursor to the next line of the current navigator object and speaks it |
| `review_nextPage` | ALUS+pageDown | ALUS+pageDown, ALUS+shift+pageDown | Moves the review cursor to the next page of the current navigator object and speaks it |
| `review_nextWord` | numpad6 | numpad6, ALUS+control+rightArrow | Moves the review cursor to the next word of the current navigator object and speaks it |
| `review_previousCharacter` | numpad1 | numpad1, ALUS+leftArrow | Moves the review cursor to the previous character of the current navigator object and speaks it |
| `review_previousLine` | numpad7 | numpad7, ALUS+upArrow | Moves the review cursor to the previous line of the current navigator object and speaks it |
| `review_previousPage` | ALUS+pageUp | ALUS+pageUp, ALUS+shift+pageUp | Moves the review cursor to the previous page of the current navigator object and speaks it |
| `review_previousWord` | numpad4 | numpad4, ALUS+control+leftArrow | Moves the review cursor to the previous word of the current navigator object and speaks it |
| `review_top` | shift+numpad7 | shift+numpad7, ALUS+control+home | Moves the review cursor to the top line of the current navigator object and speaks it |
| `showFormattingAtReview` | — | — | Presents, in browse mode, formatting info for the current review cursor position. |
| `review_sayAll` | numpadPlus | numpadPlus, ALUS+shift+a | Reads from the review cursor up to the end of the current text, moving the review cursor as it goes |
| `reportFormattingAtReview` | — | — | Reports formatting info for the current review cursor position. |
| `reportFormatting` | ALUS+shift+f | ALUS+shift+f | Reports formatting info for the current review cursor position. If pressed twice, presents the information in browse mode |
| `review_currentCharacter` | numpad2 | numpad2, ALUS+. | Reports the character of the current navigator object where the review cursor is situated. Pressing twice reports a description or example of that character. Pressing three times reports the numeric value of the character in decimal and hexadecimal |
| `review_currentLine` | numpad8 | numpad8, ALUS+shift+. | Reports the line of the current navigator object where the review cursor is situated. If this key is pressed twice, the current line will be spelled. Pressing three times will spell the line using character descriptions. |
| `review_currentSymbol` | — | — | Reports the symbol where the review cursor is positioned. Pressed twice, shows the symbol and the text used to speak it in browse mode |
| `review_currentWord` | numpad5 | numpad5, ALUS+control+. | Speaks the word of the current navigator object where the review cursor is situated. Pressing twice spells the word. Pressing three times spells the word using character descriptions |
| `reviewMode_next` | ALUS+numpad7 | ALUS+numpad7, ALUS+pageUp | Switches to the next review mode (e.g. object, document or screen) and positions the review position at the point of the navigator object |
| `reviewMode_previous` | ALUS+numpad1 | ALUS+numpad1, ALUS+pageDown | Switches to the previous review mode (e.g. object, document or screen) and positions the review position at the point of the navigator object |
| `toggleCaretMovesReviewCursor` | ALUS+6 | ALUS+6 | Toggles on and off the movement of the review cursor due to the caret moving. |

## Tools

| Command | Desktop | Laptop | Description |
|---|---|---|---|
| `activatePythonConsole` | ALUS+control+z | ALUS+control+z | **NOT IN THIS VERSION** (the key says so when pressed) — Activates the ALUS Python Console, primarily useful for development |
| `activateAddonsManager` | — | — | Activates the Add-on Store to browse and manage add-on packages for ALUS |
| `reportLinkDestinationInWindow` | — | — | Displays the destination URL of the link at the position of caret or focus in a window, instead of just speaking it. May be preferred by braille users. |
| `navigatorObject_devInfo` | ALUS+f1 | ALUS+f1 | Logs information about the current navigator object which is useful to developers and activates the log viewer so the information can be examined. |
| `log_markStartThenCopy` | ALUS+control+shift+f1 | ALUS+control+shift+f1 | **NOT IN THIS VERSION** (the key says so when pressed) — Mark the current end of the log as the start of the fragment to be copied to clipboard by pressing again. |
| `openUserConfigurationDirectory` | — | — | Opens ALUS configuration directory for the current user. |
| `startWxInspectionTool` | — | — | Opens the WX GUI inspection tool. Used to get more information about the state of GUI components. |
| `reloadPlugins` | ALUS+control+f3 | ALUS+control+f3 | **NOT IN THIS VERSION** (the key says so when pressed) — Reloads app modules and global plugins without restarting ALUS, which can be Useful for developers |
| `reportLinkDestination` | ALUS+k | ALUS+k | Report the destination URL of the link at the position of caret or focus. If pressed twice, shows the URL in a window for easier review. |
| `reportAppModuleInfo` | ALUS+control+f1 | ALUS+control+f1 | Speaks the filename of the active application along with the name of the currently loaded appModule |

---

## Where this comes from

The script names, descriptions and default keys come from the NVDA source
code, extracted by `tools/extract-commands.py` (see `docs/design/architecture.md` §5.4
and `docs/design/vision.md` §6). That is why the commands are **the same** as in NVDA:
a user coming from Windows has nothing new to learn (H1).

The descriptions are translated through gettext; `alus-cli --language sr list-commands`
shows them in Serbian.

Generated 2026-11-15.
