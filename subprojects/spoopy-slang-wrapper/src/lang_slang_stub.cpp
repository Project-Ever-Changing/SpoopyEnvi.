// Stub Slang runtime, built when the Slang runtime is disabled.
//
// Mirrors Taisei's lang_spirv_stub.c: satisfies the internal _slang_* interface
// (and owns global_context) so the always-built aux layer links, but every
// operation fails loudly.

#include "lang_slang_private.hpp"

#include <spoopy_log.h>
#include <spoopy_shader.h>

struct spoopy_context { };

extern "C" {
spoopy_context_t global_context = { };
}

bool _slang_init_compiler(void) {
	SPOOPY_LOG_ERROR("Compiled without Slang runtime support");
	return false;
}

void _slang_shutdown_compiler(void) { }

bool _slang_shader_supported(const spoopy_shader_source_t*, spoopy_transpile_options_t*) {
	return false;
}

bool _slang_compile(
	spoopy_shader_source_t*,
	spoopy_shader_source_t*,
	spoopy_transpile_options_t*,
	spoopy_mem_arena_t*
) {
	SPOOPY_LOG_ERROR("Compiled without Slang runtime support");
	return false;
}
