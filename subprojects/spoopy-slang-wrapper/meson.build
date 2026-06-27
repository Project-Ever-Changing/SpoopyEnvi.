project('spoopy-slang-wrapper', ['c', 'cpp'])

subdir('scripts')
slang_version = '2025.14.3'

fs = import('fs')
slang_dir = meson.current_source_dir() / 'external' / 'slang'
slang_paths_file = slang_dir / 'slang_paths.txt'

if not fs.exists(slang_paths_file)
    message('Downloading Slang paths binaries...')

    download_result = run_command([
        slang_fetch_script,
        slang_version,
        slang_dir,
    ], check: true)

    if download_result.returncode() != 0
        error('Failed to download Slang paths binaries. Return code: ' + download_result.returncode().to_string())
    endif
endif

slang_paths = {}
if fs.exists(slang_paths_file)
    paths_content = fs.read(slang_paths_file).strip().split('\n')

    foreach line : paths_content
        if line != ''
            parts = line.split('=')
            if parts.length() == 2
                slang_paths += {parts[0]: parts[1]}
            endif
        endif
    endforeach
else
    error('Slang paths file not found')
endif

slang_include = include_directories('external/slang/include')
# spoopy_slang_include = include_directories('../../src/renderer/src')
spoopy_include_arg = '-I' + (meson.global_source_root() / 'include')
spoopy_generated_include_arg = '-I' + (meson.global_build_root() / 'include')
bx_include = include_directories('bx/include')
bx_debug_define = '-DBX_CONFIG_DEBUG=' + (get_option('buildtype').startswith('debug') ? '1' : '0')

if host_machine.system() == 'windows'
    slang_lib = declare_dependency(
        include_directories: slang_include,
        dependencies: [
            meson.get_compiler('cpp').find_library('slang',
                dirs: [fs.parent(slang_paths['lib'])],
                static: false
            )
        ]
    )
else
    slang_lib = declare_dependency(
        include_directories: slang_include,
        link_args: [slang_paths['lib']]
    )
endif

slang_src = [
    'src/spoopy_libs_api.cpp'
]

spoopy_slang_lib = static_library('spoopy-slang',
    slang_src,
    include_directories: [include_directories('.'), slang_include, bx_include],
    cpp_args: [bx_debug_define, spoopy_include_arg, spoopy_generated_include_arg],
    dependencies: slang_lib,
    install: false,
)

spoopy_slang_dep = declare_dependency(
    link_with: spoopy_slang_lib,
    include_directories: slang_include,
    dependencies: slang_lib,
)

# Copy the slang dylib to the build directory for runtime linking
slang_dylib_source = slang_paths['lib']
slang_dylib_dest = meson.global_build_root() / 'src' / 'core' / 'libslang.dylib'

# Use a custom target to copy the dylib during build
copy_slang_dylib = custom_target('copy_slang_dylib',
    input: [],
    output: 'libslang_copied',
    command: [
        'cp', slang_dylib_source, slang_dylib_dest
    ],
    build_by_default: true,
    build_always_stale: true
)

# Export the dependency for the parent project
spoopy_slang_wrapper_dep = declare_dependency(
    link_with: spoopy_slang_lib,
    include_directories: slang_include,
    dependencies: slang_lib,
    sources: copy_slang_dylib
)
