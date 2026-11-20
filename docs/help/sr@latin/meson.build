foreach page : help_pages
  custom_target('help-sr@latin-' + page,
    input: page + '.md',
    output: page + '.html',
    command: [python3, help_builder, '@INPUT@', '@OUTPUT@', '--lang', 'sr-Latn'],
    depend_files: help_builder,
    build_by_default: true,
    install: true,
    install_dir: help_install_root / 'sr@latin')
endforeach
