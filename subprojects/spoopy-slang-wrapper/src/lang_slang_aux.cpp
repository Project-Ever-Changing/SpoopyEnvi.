// Public C-ABI surface for the Slang runtime.
//
// Always built. Mirrors Taisei's lang_spirv_aux.c: it validates inputs and
// forwards to the internal _slang_* functions, which are provided by either
// lang_slang.cpp (real) or lang_slang_stub.cpp (disabled).

#include "lang_slang_private.hpp"

#include <spoopy_api.h>
#include <spoopy_log.h>
#include <spoopy_shader.h>
#include <memory/spoopy_memory.h>

#include <cstring>

extern "C" {

bool spoopy_global_context_init(void) {
	return _slang_init_compiler();
}

void spoopy_shader_cleanup(void) {
	_slang_shutdown_compiler();
}

void spoopy_shader_source_cleanup(spoopy_shader_source_t* source) {
	if(!source) {
		return;
	}

	memset(source, 0, sizeof(*source));
}

bool spoopy_api_shader_supported(const spoopy_shader_source_t* info, spoopy_transpile_options_t* transpile_opts) {
	return _slang_shader_supported(info, transpile_opts);
}

bool spoopy_api_shader_transpile(
	spoopy_shader_source_t* source,
	spoopy_shader_source_t* target,
	spoopy_transpile_options_t* transpile_opts,
	spoopy_mem_arena_t* arena
) {
	if(!source || !target || !arena || !source->content || !source->entry_point) {
		SPOOPY_LOG_ERROR("Invalid shader transpile parameters");
		return false;
	}

	return _slang_compile(source, target, transpile_opts, arena);
}

void spoopy_api_add_macro(spoopy_transpile_options_t* options, const char* name, const char* value) {
	if(!options || !name || !value) {
		SPOOPY_LOG_ERROR("Invalid parameters for adding a shader macro");
		return;
	}

	size_t new_count = options->macro_count + 1;
	spoopy_shader_macro_t* new_macros = (spoopy_shader_macro_t*)spoopy_heap_realloc(
		options->macros,
		new_count * sizeof(*new_macros)
	);

	if(!new_macros) {
		SPOOPY_LOG_ERROR("Failed to grow shader macro array");
		return;
	}

	options->macros = new_macros;
	options->macros[options->macro_count].name = name;
	options->macros[options->macro_count].value = value;
	options->macro_count = new_count;
}

} // extern "C"
