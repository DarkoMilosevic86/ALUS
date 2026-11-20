# The HTML help the ALUS menu opens (Help -> User guide, What's new), built
# from docs/help/<lang>/*.md by tools/build-help.py and installed to
# share/doc/alus/<lang>/, where alus-menu looks for it (ALUS_DOC_DIR).
# Serbian Cyrillic has no source of its own: it is the Latin source
# transliterated, by the same rules as the program's translations.
help_builder = files('../../tools/build-help.py')
help_pages = ['user-guide', 'whats-new']
help_install_root = get_option('datadir') / 'doc' / 'alus'
subdir('en')
subdir('sr@latin')
subdir('sr')
