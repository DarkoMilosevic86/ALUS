# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Darko Milošević <daremc86@gmail.com>
#
# This file is part of ALUS (Access Linux Using Speech).
#
# The gettext catalogues.
#
# `languages` is NOT passed: meson reads po/LINGUAS, which is the file a
# translator adds a language to, so adding one touches no build file.
#
# The targets this creates, and the two the exit criteria of phase 9 name:
#   meson compile -C build alus-pot        regenerate po/alus.pot from the sources
#   meson compile -C build alus-update-po  merge the new .pot into every .po
#
# The .mo files are built as part of the default target and installed into
# $localedir, which is where i18n::initialize looks (ALUS_LOCALE_DIR).
i18n = import('i18n')

i18n.gettext('alus',
  args: [
    # The macros from include/alus/i18n/i18n.hpp. This list and that file are
    # the two places that have to agree; nothing else mentions the macros.
    #   _(s)             plain
    #   N_(s)            marked here, translated at the point of use
    #   C_(ctx, s)       with a disambiguating context
    #   NC_(ctx, s)      marked with context
    #   P_(sing, pl, n)  plural
    '--keyword=_',
    '--keyword=N_',
    '--keyword=C_:1c,2',
    '--keyword=NC_:1c,2',
    '--keyword=P_:1,2',
    # Without --keyword=  (empty), xgettext ALSO collects gettext/ngettext and
    # its own default list, which would pick up unrelated calls. Keeping the
    # defaults is deliberate here: speech-dispatcher and GLib headers define
    # none of them, and dropping them would silently lose a string the day
    # someone writes gettext() directly.
    '--from-code=UTF-8',
    # The `// Translators:` comments are the whole point of having written
    # them; without this they never reach the .pot.
    '--add-comments=Translators:',
    '--package-name=alus',
    '--package-version=' + meson.project_version(),
    '--msgid-bugs-address=daremc86@gmail.com',
    '--copyright-holder=Darko Milošević',
    # ALUS is C++20; xgettext's C parser mis-reads raw string literals and
    # `>` in templates.
    '--language=C++',
    '--sort-by-file',
  ],
  install: true)

# `msgfmt --check` on every catalogue: the phase 9 exit criterion
#   msgfmt --check po/sr.po -o /dev/null
# as a test, so a broken plural form or a placeholder a translator dropped
# fails the build instead of being found by a blind user.
msgfmt = find_program('msgfmt', required: true)
foreach language : ['sr', 'sr@latin']
  # The `external` suite and NOT `gui`, which is the phase 9 exit criterion.
  #
  # This runs /usr/bin/msgfmt, so under the valgrind setup Memcheck reports the
  # leaks in GNU gettext's own libgettextsrc (455 bytes in string_list_concat,
  # among others) and fails the run. Those are not ALUS's to fix, and not
  # ALUS's to suppress by frame either, because the frames are in another
  # project's shared library. `exclude_suites` does not help: an explicit
  # --suite on the command line overrides it, and the valgrind run names its
  # suites explicitly.
  #
  # What msgfmt checks is checked in C++ as well, over the same files, in
  # tests/gui/test_i18n.cpp -- every entry translated, and every msgstr
  # carrying the same placeholders as its msgid. So `--suite gui` is complete
  # on its own and this is the belt-and-braces run against the real tool:
  #   meson test -C build --suite external
  test('po-' + language,
    msgfmt,
    args: ['--check', '--check-accelerators', '--statistics',
           meson.current_source_dir() / (language + '.po'), '-o', '/dev/null'],
    suite: 'external')
endforeach
