# What's new in ALUS

## 2026.1.0 beta

The first public version of ALUS.

### Highlights

- A screen reader for the GNOME desktop on Wayland, with the commands,
  keys and concepts blind users know: focus, review cursor, object navigation, browse mode,
  focus mode, say all, input help.
- **The ALUS menu** (`ALUS+A`, laptop `ALUS+N`): settings, input gestures,
  help, donations, saving and restoring the configuration, exit.
- **Help in English, Serbian Cyrillic and Serbian Latin**: this guide, What's
  new, and a commands quick reference generated from the keys you really use.
- **Web browsing**: quick navigation letters read the element
  they land on; tables, lists and landmarks read their first line; the
  elements list (`ALUS+F7`) opens where you are; screen layout (`ALUS+V`) is
  remembered after a restart.
- **Focus mode**: `ALUS+Space` turns it on and off; `Escape` leaves
  only the focus mode ALUS entered by itself, so it still leaves full screen
  on YouTube.
- **Thunderbird**: messages are read in browse mode.
- **Speech** through speech-dispatcher (espeak-ng recommended, RHVoice and
  others work); voice settings can be changed without opening a window.
- **Braille** through brltty and liblouis, including Serbian tables.
- **Commands that cannot work on Wayland** -- moving the mouse pointer, for
  example -- say why, instead of being silent.
- **Clipboard commands**: read the clipboard (`ALUS+C`) and copy what ALUS
  said last (`ALUS+Control+X`).
- **Menus of GNOME applications** (Calculator, Files, Text Editor…) are
  read with the names of their items.
- **More commands without a default key**, ready to be given one in Input
  gestures: a switch for every kind of document formatting, reading,
  spelling and copying the status bar, the keyboard layout, automatic
  language switching, opening the configuration folder, restarting ALUS,
  and browse mode jumps to toggle buttons, menu items, progress bars and
  sliders.
- **Safe switching**: `alus-switch alus` swaps Orca for ALUS and back, says
  every step, and brings Orca back if ALUS fails to start. ALUS turns on
  application accessibility by itself when the desktop switch is off.

### Known limitations

- Only GNOME on Wayland has been tested; other desktops may work.
- The mouse pointer cannot be moved or clicked on Wayland; the click keys act
  on the navigator object instead.
- ALUS is a beta: please report problems on the project page,
  https://github.com/DarkoMilosevic86/ALUS.
