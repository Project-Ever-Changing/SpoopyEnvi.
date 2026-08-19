#pragma once

// Internal Slang runtime interface.
//
// Modeled after Taisei's lang_spirv split: the public C-ABI (lang_slang_aux.cpp)
// is always built and forwards to these internals, which live either in the real
// implementation (lang_slang.cpp) or in the stub (lang_slang_stub.cpp).

#include <spoopy_shader.h>

#include <memory/spoopy_arena.h>

bool _slang_init_compiler(void);
void _slang_shutdown_compiler(void);

bool _slang_shader_supported(
	const spoopy_shader_source_t* source,
	spoopy_transpile_options_t* transpile_opts
);

bool _slang_compile(
	spoopy_shader_source_t* source,
	spoopy_shader_source_t* target,
	spoopy_transpile_options_t* transpile_opts,
	spoopy_mem_arena_t* arena
);
