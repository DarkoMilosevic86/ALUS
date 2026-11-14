# ALUS documentation

## Using ALUS

- [User guide](user-guide.md): starting and stopping, the ALUS key, the most
  important commands, settings, language, speech, braille, browse mode,
  applications, changing keys, diagnostics, troubleshooting.
- [Command reference](commands.md): every command with its desktop and
  laptop keys. Also as [CSV](commands.csv). Generated from the command table.
- [Configuration](configuration.md): `alus.ini`, `gestures.ini`,
  dictionaries, profiles, environment variables.

## Getting ALUS

- [Building](building.md): dependencies for Debian, Fedora and Arch,
  compiling, build types, running from the source tree.
- [Installing](installing.md): `meson install`, what goes where, starting
  with the session, swapping screen readers, updating, uninstalling.

## Changing ALUS

- [Development](development.md): source layout, tests, observing the
  daemon, generated files, adding commands, app modules and settings,
  translations, conventions.
- [Writing add-ons](add-ons.md): the C plugin ABI.

## Design

- [Vision and scope](design/vision.md): what ALUS is and is not, hard
  requirements, binding non-goals, licence, glossary.
- [Architecture](design/architecture.md): how NVDA's architecture maps onto
  Linux, subsystem by subsystem, with the deviations measurement forced.
- [Risk register](design/risks.md): what could sink the project and what was
  done about it.

## History

- [Phase reports and findings](history/README.md) (Serbian): how ALUS was
  built, and the findings N1–N128 that the code cites.
