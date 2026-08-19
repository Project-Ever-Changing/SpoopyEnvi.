spoopy_memory_deps = []
spoopy_memory_src  = files(
	'spoopy_arena_memory.c',
	'spoopy_core_memory.c',
)

allocator = get_option('allocator')
if allocator == 'default'
	spoopy_memory_src += files('spoopy_backend_mimalloc.c')
	dep_mimalloc = dependency(
		'mimalloc',
		required: get_option('need_mimalloc'),
		fallback: ['mimalloc-cmake-wrapper', 'mimalloc_dep'],
	)
	spoopy_memory_deps += dep_mimalloc
elif allocator == 'custom'
	message('Using custom allocator implementation')
else
	error('Unknown allocator option: @0@'.format(allocator))
endif
