# Only build tests if the option is enabled
opt_tests = get_option('enable_tests')

if opt_tests.disabled()
    subdir_done()
endif

tests = [
    'triangle',
    'texture',
    'cube',
    'audio',
]

if host_machine.system() != 'emscripten'
    tests += ['parallel']
endif

foreach test : tests
    executable(test,
        '@0@.c'.format(test),
        dependencies: spoopy_deps, # TODO (Framework): Can't have deps for the framework portion
        link_with: [spoopy_core_lib],
        link_language: 'c',
        include_directories: spoopy_includes,
        install: false,
    )
endforeach
