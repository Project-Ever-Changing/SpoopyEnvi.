project('spoopy', ['c'],
    license: 'mit',
    meson_version: '>=1.5.2',
    version: run_command([
        'node', '-e', 'console.log(require("./package.json").version)'
    ], check: true).stdout().strip(),
    default_options: run_command([
        files('scripts/default_options.js'),
        '--language', 'c',
        '--build-type', 'auto',
    ], check: true).stdout().strip().split('\n')
)

fs = import('fs')

cc = meson.get_compiler('c')
config = configuration_data()

if host_machine.system() == 'darwin'
    add_languages('objc', native: false)

    macos_min_version = meson.get_external_property('macos_min', '10.15')
    macos_min_arg = '-mmacosx-version-min=' + macos_min_version
    add_project_arguments(macos_min_arg, language: ['c', 'objc'])
    add_project_link_arguments(macos_min_arg, language: ['c', 'objc'])
endif

is_debug_build = get_option('debug')
deprecation_warnings = get_option('deprecation_warnings')

spoopy_public_includes = include_directories('include', 'include')

subdir('src/core/renderer/config')
subdir('include')
spoopy_includes = [spoopy_public_includes, spoopy_generated_includes]

spoopy_deps = []
if host_machine.system() == 'darwin'
    spoopy_deps += dependency('appleframeworks', modules: ['AppKit', 'Cocoa', 'Foundation', 'Metal', 'QuartzCore'])
endif

dep_sdl3 = dependency('sdl3', version: '>=3.4.0', fallback : ['sdl3-cmake-wrapper', 'sdl3_dep'])
dep_slang = dependency('spoopy-slang-wrapper', fallback : ['spoopy-slang-wrapper', 'spoopy_slang_wrapper_dep'], required: true)
dep_cglm  = dependency('cglm', version : '>=0.7.8', required: true)

spoopy_deps += [
	dep_sdl3,
	dep_slang,
	dep_cglm,
]

fastnoiselite_dep = disabler()
if get_option('fastnoiselite')
    fastnoiselite_dep = dependency('fastnoiselite', fallback: ['fastnoiselite', 'fastnoiselite_dep'], required: true)
endif

has_posix = cc.has_header_symbol('unistd.h', '_POSIX_VERSION')

if has_posix
    if not cc.has_header_symbol('stdio.h', 'fileno')
        config.set('_POSIX_C_SOURCE', '200809L')
    endif

    if host_machine.system() == 'darwin'
        config.set('_DARWIN_C_SOURCE', true)
    endif
endif

config.set('SPOOPY_HAS_POSIX', has_posix)
config.set('SPOOPY_HAS_ALIGNED_ALLOC', cc.has_function('aligned_alloc'))
config.set('SPOOPY_HAS_POSIX_MEMALIGN',
    cc.has_function('posix_memalign') and host_machine.system() != 'nx')

if host_machine.system() != 'emscripten'
    malloc_alignment = cc.alignment('max_align_t', prefix: '#include <stddef.h>\n')
    config.set('SPOOPY_MALLOC_ALIGNMENT', malloc_alignment)
endif

config.set('SPOOPY_BUILD_DEBUG', is_debug_build)

foreach header : ['stdint.h', 'stdbool.h', 'string.h', 'stdlib.h', 'stdio.h']
    if cc.has_header(header)
        config.set('SPOOPY_HAS_@0@'.format(header.underscorify().to_upper()), 1)
    endif
endforeach

systype = '@0@, @1@, @2@'.format(
    host_machine.system(),
    host_machine.cpu_family(),
    host_machine.cpu()
)

if meson.is_cross_build()
    systype = '@0@ (cross-compiled)'.format(systype)
endif


# Get absolute paths
# "Borrowed" from Sway desktop environment, also, very confused about what
# this was for, so I wrote comments to explain each process
# for others' planning to "borrow" this code.

source_root = meson.current_source_dir()    # e.g., "/home/user/spoopy"
build_root = meson.project_build_root()     # e.g., "/home/user/spoopy/builddir"

if build_machine.system() == 'windows'
    add_languages('cpp')

    source_root = source_root.replace('\\', '/')  # C:\project → C:/project
    build_root = build_root.replace('\\', '/')    # C:\project\build → C:/project/build
endif

source_root = source_root.split('/')  # ["/home", "user", "spoopy"]
build_root = build_root.split('/')    # ["/home", "user", "spoopy", "builddir"]

relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
    if i >= source_root.length() or not in_prefix or p != source_root[i]
        in_prefix = false
        relative_dir_parts += '..'
    endif
    i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
    if i >= build_root.length() or not in_prefix or build_root[i] != p
        in_prefix = false
        relative_dir_parts += p
    endif
    i += 1
endforeach

relative_dir = join_paths(relative_dir_parts)
relative_src_dir = relative_dir + '/src/'

if cc.has_argument('-fmacro-prefix-map=/prefix/to/hide=')
    add_project_arguments(
        '-fmacro-prefix-map=@0@=.'.format(relative_src_dir),
        language: 'c',
    )
else
    config.set_quoted('SPOOPY_REAL_DIR', relative_dir)
    config.set_quoted('SPOOPY_REAL_SRC_DIR', relative_src_dir)
endif

subdir('scripts')
subdir('src')
subdir('test')

spoopy_sys_threads_support = get_option('have_sdl_thread_support')

if not spoopy_sys_threads_support
    config.set('SPOOPY_SUPPORT_SDL_THREADS', 0)
endif

summary({
    'Version' : meson.project_version(),
    'System type' : systype,
    'Build type' : get_option('buildtype'),
    'Memory allocator' : allocator,
    'Debug build' : is_debug_build,
}, section : 'Main', bool_yn : true)

summary({
    'Audio backends' : '@0@ (default: @1@)'.format(', '.join(enabled_audio_backends), default_audio_backend),
    'Rendering backends' : '@0@ (default: @1@)'.format(', '.join(enabled_renderer_backends), default_renderer_backend),
    'FastNoiseLite' : get_option('fastnoiselite'),
    'SDL threading' : spoopy_sys_threads_support,
    'Deprecation warnings' : deprecation_warnings,
}, section : 'Features', bool_yn : true)
