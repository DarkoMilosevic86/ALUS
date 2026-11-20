foreach page : help_pages
  custom_target('help-sr-' + page,
    input: '../sr@latin' / page + '.md',
    output: page + '.html',
    command: [python3, help_builder, '@INPUT@', '@OUTPUT@', '--lang', 'sr-Cyrl', '--cyrillic'],
    depend_files: [help_builder, files('../../../tools/import-nvda-po.py')],
    build_by_default: true,
    install: true,
    install_dir: help_install_root / 'sr')
endforeach
