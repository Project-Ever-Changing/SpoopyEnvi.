// Real Slang runtime implementation.
//
// Structured after Taisei's lang_spirv.c: file-local helpers do the work,
// reflection is split into _slang_reflect_{ubos,samplers,inputs} orchestrated by
// _slang_reflect_all, and the small _slang_* interface (declared in
// lang_slang_private.hpp) is what the public aux layer calls into.

#include "lang_slang_private.hpp"
#include "libs.hpp"

#include <slang.h>
#include <slang-com-ptr.h>

#include <memory/spoopy_arena.h>
#include <memory/spoopy_memory.h>
#include <spoopy_graphics.h>
#include <spoopy_log.h>
#include <spoopy_reflect.h>
#include <spoopy_shader.h>
#include <spoopy_types.h>

#include <cstring>
#include <initializer_list>

using namespace slang;

struct spoopy_context {
	Slang::ComPtr<slang::IGlobalSession> global_session;
};

extern "C" {
spoopy_context_t global_context = { };
}

namespace {

struct target_profile {
	spoopy_renderer_t renderer;
	SlangCompileTarget target;
	SlangProfileID profile;
};

struct reflection_field_tmp {
	tiny_string name;
	spoopy_data_type_t type;
	uint16_t offset;
};

struct reflection_block_tmp {
	tiny_string name;
	uint16_t set;
	uint16_t binding;
	uint16_t size;
	tinystl::vector<reflection_field_tmp> fields;
};

struct reflection_sampler_tmp {
	tiny_string name;
	ShaderSamplerType type;
	uint16_t set;
	uint16_t binding;
	uint16_t array_size;
};

struct reflection_input_tmp {
	tiny_string name;
	uint16_t location;
	uint16_t num_locations_consumed;
};

struct slang_reflect_context {
	spoopy_mem_arena_t* arena;
	slang::ProgramLayout* program_layout;
	tinystl::vector<reflection_block_tmp> blocks;
	tinystl::vector<reflection_sampler_tmp> samplers;
	tinystl::vector<reflection_input_tmp> inputs;
};

static size_t clamp_u16(size_t value) {
	return value > UINT16_MAX ? UINT16_MAX : value;
}

static bool is_valid_name(const char* name) {
	return name != NULL && name[0] != '\0';
}

static size_t shader_source_size(const spoopy_shader_source_t* source) {
	if(!source || !source->content) {
		return 0;
	}

	if(source->content_size > 0) {
		return source->content_size;
	}

	return strlen(source->content);
}

static void* arena_alloc(spoopy_mem_arena_t* arena, size_t size) {
	if(!arena || size == 0) {
		return NULL;
	}

	return spoopy_arena_alloc(arena, size);
}

static char* arena_memdup_string(spoopy_mem_arena_t* arena, const void* data, size_t size) {
	char* out = (char*)arena_alloc(arena, size + 1);

	if(!out) {
		return NULL;
	}

	if(size > 0 && data) {
		memcpy(out, data, size);
	}

	out[size] = '\0';
	return out;
}

static void log_diagnostics(const char* prefix, slang::IBlob* diagnostics) {
	if(!diagnostics) {
		return;
	}

	const char* text = (const char*)diagnostics->getBufferPointer();
	if(text && text[0] != '\0') {
		SPOOPY_LOG_ERROR("%s%s", prefix, text);
	}
}

static inline SlangProfileID try_find_profile(
	slang::IGlobalSession* global_session,
	std::initializer_list<const char*> candidates
) {
	for(const char* name : candidates) {
		if(!name) {
			continue;
		}

		SlangProfileID id = global_session->findProfile(name);
		if(id != SLANG_PROFILE_UNKNOWN) {
			return id;
		}
	}

	return SLANG_PROFILE_UNKNOWN;
}

static void log_profile_candidates(
	slang::IGlobalSession* global_session,
	const char* label,
	std::initializer_list<const char*> candidates
) {
	if(!global_session || !label) {
		return;
	}

	bool found_any = false;
	for(const char* name : candidates) {
		if(!name) {
			continue;
		}

		SlangProfileID id = global_session->findProfile(name);
		if(id != SLANG_PROFILE_UNKNOWN) {
			found_any = true;
			SPOOPY_LOG_INFO("Slang profile available (%s): %s", label, name);
		}
	}

	if(!found_any) {
		SPOOPY_LOG_WARN("No Slang profiles found for %s candidates", label);
	}
}

static target_profile pick_target_profile(slang::IGlobalSession* global_session, spoopy_renderer_t renderer_mask) {
	target_profile out = { SPOOPY_RENDERER_API_UNSURE, SLANG_TARGET_UNKNOWN, SLANG_PROFILE_UNKNOWN };

	spoopy_renderer_t renderer = spoopy_graphics_pick_renderer(renderer_mask);
	if(!spoopy_graphics_renderer_supported(renderer)) {
		return out;
	}

	out.renderer = renderer;

	switch(renderer) {
		case SPOOPY_RENDERER_API_METAL:
			out.target = SLANG_METAL;
			out.profile = try_find_profile(global_session, {
				"metallib_3_1", "metallib_3_0",
				"metallib_2_4", "metallib_2_3", "metallib_2_2", "metallib_2_1", "metallib_2_0",
				"metal",
				"metal_3_1", "metal_3_0",
				"metal_2_4", "metal_2_3", "metal_2_2", "metal_2_1", "metal_2_0"
			});
			return out;

		case SPOOPY_RENDERER_API_WGPU:
			out.target = SLANG_WGSL;
			out.profile = try_find_profile(global_session, { "wgsl", "wgsl_1_0" });
			return out;

		default:
			break;
	}

	return out;
}

static SlangOptimizationLevel map_optimization_level(spoopy_optimization_level_t level) {
	switch(level) {
		case SPOOPY_OPTIMIZATION_LEVEL_NONE:    return SLANG_OPTIMIZATION_LEVEL_NONE;
		case SPOOPY_OPTIMIZATION_LEVEL_DEFAULT: return SLANG_OPTIMIZATION_LEVEL_DEFAULT;
		case SPOOPY_OPTIMIZATION_LEVEL_HIGH:    return SLANG_OPTIMIZATION_LEVEL_HIGH;
		case SPOOPY_OPTIMIZATION_LEVEL_MAXIMAL: return SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
		default:                                return SLANG_OPTIMIZATION_LEVEL_NONE;
	}
}

static bool resolve_supported_target(
	const spoopy_shader_source_t* source,
	spoopy_transpile_options_t* transpile_opts,
	target_profile* out_profile
) {
	target_profile profile = pick_target_profile(global_context.global_session.get(), source->target);
	if(profile.target == SLANG_TARGET_UNKNOWN) {
		if(transpile_opts) {
			transpile_opts->compile.renderer = SPOOPY_RENDERER_API_UNSURE;
			transpile_opts->compile.target = SLANG_TARGET_UNKNOWN;
			transpile_opts->compile.profile = SLANG_PROFILE_UNKNOWN;
		}

		return false;
	}

	if(profile.profile == SLANG_PROFILE_UNKNOWN) {
		if(transpile_opts) {
			transpile_opts->compile.renderer = profile.renderer;
			transpile_opts->compile.target = profile.target;
			transpile_opts->compile.profile = SLANG_PROFILE_UNKNOWN;
		}

		return false;
	}

	if(transpile_opts) {
		transpile_opts->compile.renderer = profile.renderer;
		transpile_opts->compile.target = profile.target;
		transpile_opts->compile.profile = profile.profile;
		transpile_opts->stage = source->stage;
	}

	if(out_profile) {
		*out_profile = profile;
	}

	return true;
}

static void configure_target_desc(slang::TargetDesc& target_desc, SlangCompileTarget target) {
	target_desc.lineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_STANDARD;
	target_desc.flags = 0;

	if(target == SLANG_METAL) {
		target_desc.lineDirectiveMode = SLANG_LINE_DIRECTIVE_MODE_NONE;
	}
}

static void add_int_compiler_option(
	tinystl::vector<slang::CompilerOptionEntry>& options,
	slang::CompilerOptionName name,
	int32_t value
) {
	slang::CompilerOptionEntry entry = { };
	entry.name = name;
	entry.value.kind = slang::CompilerOptionValueKind::Int;
	entry.value.intValue0 = value;
	entry.value.intValue1 = 0;
	entry.value.stringValue0 = NULL;
	entry.value.stringValue1 = NULL;
	options.push_back(entry);
}

static char* build_source_with_macros(
	const spoopy_shader_source_t* source,
	const spoopy_transpile_options_t* options
) {
	size_t src_size = shader_source_size(source);
	size_t macro_prefix_size = 0;

	if(options && options->macros) {
		for(size_t i = 0; i < options->macro_count; ++i) {
			const spoopy_shader_macro_t* macro = &options->macros[i];
			if(!macro->name || !macro->value) {
				continue;
			}

			macro_prefix_size += sizeof("#define \n") - 1;
			macro_prefix_size += strlen(macro->name);
			macro_prefix_size += strlen(macro->value);
		}
	}

	char* text = (char*)spoopy_heap_alloc(macro_prefix_size + src_size + 1);
	if(!text) {
		return NULL;
	}

	char* cursor = text;

	if(options && options->macros) {
		for(size_t i = 0; i < options->macro_count; ++i) {
			const spoopy_shader_macro_t* macro = &options->macros[i];
			if(!macro->name || !macro->value) {
				continue;
			}

			memcpy(cursor, "#define ", sizeof("#define ") - 1);
			cursor += sizeof("#define ") - 1;

			size_t name_len = strlen(macro->name);
			memcpy(cursor, macro->name, name_len);
			cursor += name_len;

			*cursor++ = ' ';

			size_t value_len = strlen(macro->value);
			memcpy(cursor, macro->value, value_len);
			cursor += value_len;

			*cursor++ = '\n';
		}
	}

	if(src_size > 0 && source && source->content) {
		memcpy(cursor, source->content, src_size);
		cursor += src_size;
	}

	*cursor = '\0';
	return text;
}

static spoopy_shader_base_type_t reflect_scalar_type(slang::TypeReflection::ScalarType scalar_type) {
	switch(scalar_type) {
		case slang::TypeReflection::ScalarType::Void:    return SPOOPY_SHADER_BASE_TYPE_VOID;
		case slang::TypeReflection::ScalarType::Bool:    return SPOOPY_SHADER_BASE_TYPE_BOOLEAN;
		case slang::TypeReflection::ScalarType::Int8:    return SPOOPY_SHADER_BASE_TYPE_INT8;
		case slang::TypeReflection::ScalarType::UInt8:   return SPOOPY_SHADER_BASE_TYPE_UINT8;
		case slang::TypeReflection::ScalarType::Int16:   return SPOOPY_SHADER_BASE_TYPE_INT16;
		case slang::TypeReflection::ScalarType::UInt16:  return SPOOPY_SHADER_BASE_TYPE_UINT16;
		case slang::TypeReflection::ScalarType::Int32:   return SPOOPY_SHADER_BASE_TYPE_INT32;
		case slang::TypeReflection::ScalarType::UInt32:  return SPOOPY_SHADER_BASE_TYPE_UINT32;
		case slang::TypeReflection::ScalarType::Int64:   return SPOOPY_SHADER_BASE_TYPE_INT64;
		case slang::TypeReflection::ScalarType::UInt64:  return SPOOPY_SHADER_BASE_TYPE_UINT64;
		case slang::TypeReflection::ScalarType::Float16: return SPOOPY_SHADER_BASE_TYPE_FP16;
		case slang::TypeReflection::ScalarType::Float32: return SPOOPY_SHADER_BASE_TYPE_FP32;
		case slang::TypeReflection::ScalarType::Float64: return SPOOPY_SHADER_BASE_TYPE_FP64;
		default:                                         return SPOOPY_SHADER_BASE_TYPE_UNKNOWN;
	}
}

static spoopy_shader_sampler_dimension_t sampler_dimension_from_shape(SlangResourceShape shape) {
	switch(shape & SLANG_RESOURCE_BASE_SHAPE_MASK) {
		case SLANG_TEXTURE_1D:     return SPOOPY_SHADER_SAMPLER_DIM_1D;
		case SLANG_TEXTURE_2D:     return SPOOPY_SHADER_SAMPLER_DIM_2D;
		case SLANG_TEXTURE_3D:     return SPOOPY_SHADER_SAMPLER_DIM_3D;
		case SLANG_TEXTURE_CUBE:   return SPOOPY_SHADER_SAMPLER_DIM_CUBE;
		case SLANG_TEXTURE_BUFFER: return SPOOPY_SHADER_SAMPLER_DIM_BUFFER;
		default:                   return SPOOPY_SHADER_SAMPLER_DIM_UNKNOWN;
	}
}

static ShaderSamplerType reflect_sampler_type(slang::TypeLayoutReflection* type_layout) {
	ShaderSamplerType out = { };

	if(!type_layout) {
		return out;
	}

	slang::TypeLayoutReflection* leaf_layout = type_layout->unwrapArray();
	slang::TypeReflection* type = leaf_layout ? leaf_layout->getType() : NULL;
	if(!type) {
		return out;
	}

	if(type->getKind() != slang::TypeReflection::Kind::Resource) {
		out.dim = SPOOPY_SHADER_SAMPLER_DIM_UNKNOWN;
		out.flags = 0;
		return out;
	}

	SlangResourceShape shape = type->getResourceShape();
	out.dim = sampler_dimension_from_shape(shape);
	out.flags = 0;

	if(shape & SLANG_TEXTURE_SHADOW_FLAG) {
		out.flags |= SHADER_SAMPLER_DEPTH;
	}
	if(shape & SLANG_TEXTURE_ARRAY_FLAG) {
		out.flags |= SHADER_SAMPLER_ARRAYED;
	}
	if(shape & SLANG_TEXTURE_MULTISAMPLE_FLAG) {
		out.flags |= SHADER_SAMPLER_MULTISAMPLED;
	}

	return out;
}

static spoopy_data_type_t reflect_data_type(slang::TypeLayoutReflection* type_layout) {
	spoopy_data_type_t out = { };
	if(!type_layout) {
		return out;
	}

	if(type_layout->isArray()) {
		out.array_size = (uint16_t)clamp_u16(type_layout->getTotalArrayElementCount());
		out.array_stride = (uint16_t)clamp_u16(type_layout->getElementStride((SlangParameterCategory)slang::ParameterCategory::Uniform));
		type_layout = type_layout->unwrapArray();
	}

	slang::TypeReflection* type = type_layout->getType();
	if(!type) {
		return out;
	}

	switch(type->getKind()) {
		case slang::TypeReflection::Kind::Scalar:
			out.base_type = reflect_scalar_type(type->getScalarType());
			out.vector_size = 1;
			break;

		case slang::TypeReflection::Kind::Vector: {
			unsigned rows = type->getRowCount();
			unsigned cols = type->getColumnCount();
			out.base_type = reflect_scalar_type(type->getScalarType());
			out.vector_size = (uint16_t)clamp_u16(rows > cols ? rows : cols);
			break;
		}

		case slang::TypeReflection::Kind::Matrix: {
			unsigned rows = type->getRowCount();
			unsigned cols = type->getColumnCount();
			size_t size = type_layout->getSize(slang::ParameterCategory::Uniform);

			out.base_type = reflect_scalar_type(type->getScalarType());
			out.vector_size = (uint16_t)clamp_u16(rows > 0 ? rows : 1);
			out.matrix_columns = (uint16_t)clamp_u16(cols > 0 ? cols : 1);
			out.matrix_stride = cols > 0 ? (uint16_t)clamp_u16(size / cols) : 0;
			break;
		}

		case slang::TypeReflection::Kind::Struct:
			out.base_type = SPOOPY_SHADER_BASE_TYPE_STRUCT;
			break;

		case slang::TypeReflection::Kind::Resource:
			out.base_type = SPOOPY_SHADER_BASE_TYPE_SAMPLED_IMAGE;
			break;

		case slang::TypeReflection::Kind::SamplerState:
			out.base_type = SPOOPY_SHADER_BASE_TYPE_SAMPLER;
			break;

		default:
			out.base_type = SPOOPY_SHADER_BASE_TYPE_UNKNOWN;
			break;
	}

	return out;
}

static bool has_category(slang::VariableLayoutReflection* var_layout, slang::ParameterCategory category) {
	if(!var_layout) {
		return false;
	}

	unsigned count = var_layout->getCategoryCount();
	for(unsigned i = 0; i < count; ++i) {
		if(var_layout->getCategoryByIndex(i) == category) {
			return true;
		}
	}

	return false;
}

static bool is_resource_like(slang::TypeLayoutReflection* type_layout) {
	if(!type_layout) {
		return false;
	}

	slang::TypeReflection::Kind kind = type_layout->unwrapArray()->getKind();
	return
		kind == slang::TypeReflection::Kind::Resource ||
		kind == slang::TypeReflection::Kind::SamplerState ||
		kind == slang::TypeReflection::Kind::TextureBuffer ||
		kind == slang::TypeReflection::Kind::DynamicResource;
}

static bool is_block_like(slang::TypeLayoutReflection* type_layout) {
	if(!type_layout) {
		return false;
	}

	slang::TypeReflection::Kind kind = type_layout->unwrapArray()->getKind();
	return
		kind == slang::TypeReflection::Kind::ConstantBuffer ||
		kind == slang::TypeReflection::Kind::ParameterBlock;
}

static slang::TypeLayoutReflection* get_recurse_layout(slang::TypeLayoutReflection* type_layout) {
	if(!type_layout) {
		return NULL;
	}

	type_layout = type_layout->unwrapArray();
	slang::TypeReflection::Kind kind = type_layout->getKind();

	if((kind == slang::TypeReflection::Kind::ConstantBuffer ||
		kind == slang::TypeReflection::Kind::ParameterBlock) &&
		type_layout->getElementTypeLayout()) {
		return type_layout->getElementTypeLayout();
	}

	return type_layout;
}

static void collect_sampler_bindings_recursive(
	slang::VariableLayoutReflection* var_layout,
	tinystl::vector<reflection_sampler_tmp>& samplers
) {
	if(!var_layout) {
		return;
	}

	slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();
	if(!type_layout) {
		return;
	}

	if(is_resource_like(type_layout)) {
		const char* name = var_layout->getName();
		if(!is_valid_name(name)) {
			return;
		}

		reflection_sampler_tmp sampler = { };
		sampler.name = tiny_string(name);
		sampler.type = reflect_sampler_type(type_layout);
		sampler.set = (uint16_t)clamp_u16(var_layout->getBindingSpace());
		sampler.binding = (uint16_t)clamp_u16(var_layout->getBindingIndex());
		sampler.array_size = (uint16_t)clamp_u16(type_layout->getTotalArrayElementCount());

		samplers.push_back(sampler);
		return;
	}

	slang::TypeLayoutReflection* recurse_layout = get_recurse_layout(type_layout);
	if(!recurse_layout) {
		return;
	}

	unsigned field_count = recurse_layout->getFieldCount();
	for(unsigned i = 0; i < field_count; ++i) {
		collect_sampler_bindings_recursive(recurse_layout->getFieldByIndex(i), samplers);
	}
}

static reflection_field_tmp make_field(slang::VariableLayoutReflection* var_layout) {
	reflection_field_tmp field = { };

	field.name = tiny_string(var_layout && var_layout->getName() ? var_layout->getName() : "");
	field.offset = var_layout ? (uint16_t)clamp_u16(var_layout->getOffset(slang::ParameterCategory::Uniform)) : 0;
	field.type = reflect_data_type(var_layout ? var_layout->getTypeLayout() : NULL);
	return field;
}

static bool is_loose_uniform_field(slang::VariableLayoutReflection* var_layout) {
	if(!var_layout) {
		return false;
	}

	slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();
	if(!type_layout) {
		return false;
	}

	if(is_resource_like(type_layout) || is_block_like(type_layout)) {
		return false;
	}

	return true;
}

static void collect_uniform_blocks(
	slang::VariableLayoutReflection* globals_layout,
	tinystl::vector<reflection_block_tmp>& blocks
) {
	if(!globals_layout) {
		return;
	}

	slang::TypeLayoutReflection* globals_type = globals_layout->getTypeLayout();
	if(!globals_type) {
		return;
	}

	tinystl::vector<reflection_field_tmp> loose_fields;
	unsigned field_count = globals_type->getFieldCount();

	for(unsigned i = 0; i < field_count; ++i) {
		slang::VariableLayoutReflection* field = globals_type->getFieldByIndex(i);
		if(!field) {
			continue;
		}

		slang::TypeLayoutReflection* field_type = field->getTypeLayout();
		if(!field_type) {
			continue;
		}

		if(is_block_like(field_type)) {
			slang::TypeLayoutReflection* block_layout = get_recurse_layout(field_type);
			if(!block_layout) {
				continue;
			}

			reflection_block_tmp block = { };
			block.name = tiny_string(field->getName() ? field->getName() : "SpoopyGlobalUniforms");
			block.set = (uint16_t)clamp_u16(field->getBindingSpace());
			block.binding = (uint16_t)clamp_u16(field->getBindingIndex());
			block.size = (uint16_t)clamp_u16(block_layout->getSize(slang::ParameterCategory::Uniform));

			unsigned block_field_count = block_layout->getFieldCount();
			for(unsigned j = 0; j < block_field_count; ++j) {
				slang::VariableLayoutReflection* block_field = block_layout->getFieldByIndex(j);
				if(!block_field || !is_valid_name(block_field->getName())) {
					continue;
				}

				if(is_resource_like(block_field->getTypeLayout())) {
					continue;
				}

				block.fields.push_back(make_field(block_field));
			}

			if(block.fields.size() > 0 || block.size > 0) {
				blocks.push_back(block);
			}

			continue;
		}

		if(is_loose_uniform_field(field) && is_valid_name(field->getName())) {
			loose_fields.push_back(make_field(field));
		}
	}

	if(loose_fields.size() > 0) {
		reflection_block_tmp block = { };
		block.name = tiny_string("SpoopyGlobalUniforms");
		block.set = (uint16_t)clamp_u16(globals_layout->getBindingSpace());
		block.binding = (uint16_t)clamp_u16(globals_layout->getBindingIndex());
		block.size = (uint16_t)clamp_u16(globals_type->getSize(slang::ParameterCategory::Uniform));
		block.fields = loose_fields;
		blocks.push_back(block);
	}
}

static void collect_input_fields_recursive(
	slang::VariableLayoutReflection* var_layout,
	tinystl::vector<reflection_input_tmp>& inputs
) {
	if(!var_layout) {
		return;
	}

	slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();
	if(!type_layout) {
		return;
	}

	if(is_resource_like(type_layout) || is_block_like(type_layout)) {
		return;
	}

	slang::TypeLayoutReflection* recurse_layout = get_recurse_layout(type_layout);
	if(recurse_layout && recurse_layout->getKind() == slang::TypeReflection::Kind::Struct) {
		unsigned field_count = recurse_layout->getFieldCount();
		for(unsigned i = 0; i < field_count; ++i) {
			collect_input_fields_recursive(recurse_layout->getFieldByIndex(i), inputs);
		}
		return;
	}

	if(!(has_category(var_layout, slang::ParameterCategory::VaryingInput) ||
		has_category(var_layout, slang::ParameterCategory::MetalAttribute))) {
		return;
	}

	const char* name = var_layout->getName();
	if(!is_valid_name(name)) {
		return;
	}

	slang::TypeReflection* type = recurse_layout ? recurse_layout->getType() : NULL;
	unsigned locations = 1;
	if(type) {
		unsigned cols = type->getColumnCount();
		locations = cols > 0 ? cols : 1;
	}

	reflection_input_tmp input = { };
	input.name = tiny_string(name);
	input.location = (uint16_t)clamp_u16(var_layout->getBindingIndex());
	input.num_locations_consumed = (uint16_t)clamp_u16(locations);
	inputs.push_back(input);
}

static spoopy_shader_reflection_t* materialize_reflection(
	spoopy_mem_arena_t* arena,
	const tinystl::vector<reflection_block_tmp>& blocks,
	const tinystl::vector<reflection_sampler_tmp>& samplers,
	const tinystl::vector<reflection_input_tmp>& inputs
) {
	spoopy_shader_reflection_t* reflection = (spoopy_shader_reflection_t*)arena_alloc(arena, sizeof(*reflection));
	if(!reflection) {
		return NULL;
	}

	memset(reflection, 0, sizeof(*reflection));

	reflection->num_uniform_buffers = (uint16_t)clamp_u16(blocks.size());
	reflection->num_samplers = (uint16_t)clamp_u16(samplers.size());
	reflection->num_inputs = (uint16_t)clamp_u16(inputs.size());

	if(reflection->num_uniform_buffers > 0) {
		reflection->uniform_buffers = (spoopy_shader_block_t*)arena_alloc(
			arena,
			sizeof(*reflection->uniform_buffers) * reflection->num_uniform_buffers
		);
		if(!reflection->uniform_buffers) {
			return NULL;
		}

		memset(reflection->uniform_buffers, 0, sizeof(*reflection->uniform_buffers) * reflection->num_uniform_buffers);

		for(uint16_t i = 0; i < reflection->num_uniform_buffers; ++i) {
			const reflection_block_tmp& src = blocks[i];
			spoopy_shader_block_t* dst = &reflection->uniform_buffers[i];

			dst->name = spoopy_arena_strdup(arena, src.name.c_str());
			dst->set = src.set;
			dst->binding = src.binding;
			dst->size = src.size;
			dst->num_fields = (uint16_t)clamp_u16(src.fields.size());

			if(dst->num_fields > 0) {
				dst->fields = (spoopy_shader_struct_field_t*)arena_alloc(
					arena,
					sizeof(*dst->fields) * dst->num_fields
				);
				if(!dst->fields) {
					return NULL;
				}

				memset(dst->fields, 0, sizeof(*dst->fields) * dst->num_fields);

				for(uint16_t j = 0; j < dst->num_fields; ++j) {
					dst->fields[j].name = spoopy_arena_strdup(arena, src.fields[j].name.c_str());
					dst->fields[j].offset = src.fields[j].offset;
					dst->fields[j].type = src.fields[j].type;
				}
			}
		}
	}

	if(reflection->num_samplers > 0) {
		reflection->samplers = (spoopy_shader_sampler_t*)arena_alloc(
			arena,
			sizeof(*reflection->samplers) * reflection->num_samplers
		);
		if(!reflection->samplers) {
			return NULL;
		}

		memset(reflection->samplers, 0, sizeof(*reflection->samplers) * reflection->num_samplers);

		for(uint16_t i = 0; i < reflection->num_samplers; ++i) {
			reflection->samplers[i].name = spoopy_arena_strdup(arena, samplers[i].name.c_str());
			reflection->samplers[i].type = samplers[i].type;
			reflection->samplers[i].set = samplers[i].set;
			reflection->samplers[i].binding = samplers[i].binding;
			reflection->samplers[i].array_size = samplers[i].array_size;
		}
	}

	if(reflection->num_inputs > 0) {
		reflection->inputs = (spoopy_shader_input_t*)arena_alloc(
			arena,
			sizeof(*reflection->inputs) * reflection->num_inputs
		);
		if(!reflection->inputs) {
			return NULL;
		}

		memset(reflection->inputs, 0, sizeof(*reflection->inputs) * reflection->num_inputs);

		for(uint16_t i = 0; i < reflection->num_inputs; ++i) {
			reflection->inputs[i].name = spoopy_arena_strdup(arena, inputs[i].name.c_str());
			reflection->inputs[i].location = inputs[i].location;
			reflection->inputs[i].num_locations_consumed = inputs[i].num_locations_consumed;
		}
	}

	return reflection;
}

// Reflection passes, mirroring Taisei's _spirv_reflect_{ubos,samplers,inputs}.

static void _slang_reflect_ubos(slang_reflect_context* rctx) {
	slang::VariableLayoutReflection* globals_layout = rctx->program_layout->getGlobalParamsVarLayout();
	if(globals_layout) {
		collect_uniform_blocks(globals_layout, rctx->blocks);
	}
}

static void _slang_reflect_samplers(slang_reflect_context* rctx) {
	slang::VariableLayoutReflection* globals_layout = rctx->program_layout->getGlobalParamsVarLayout();
	if(globals_layout) {
		collect_sampler_bindings_recursive(globals_layout, rctx->samplers);
	}
}

static void _slang_reflect_inputs(slang_reflect_context* rctx) {
	if(rctx->program_layout->getEntryPointCount() == 0) {
		return;
	}

	slang::EntryPointReflection* entry_point = rctx->program_layout->getEntryPointByIndex(0);
	if(!entry_point) {
		return;
	}

	unsigned param_count = entry_point->getParameterCount();
	for(unsigned i = 0; i < param_count; ++i) {
		collect_input_fields_recursive(entry_point->getParameterByIndex(i), rctx->inputs);
	}
}

static void _slang_reflect_all(slang_reflect_context* rctx) {
	_slang_reflect_ubos(rctx);
	_slang_reflect_samplers(rctx);
	_slang_reflect_inputs(rctx);
}

static spoopy_shader_reflection_t* build_reflection(
	spoopy_mem_arena_t* arena,
	slang::IComponentType* linked_program
) {
	if(!linked_program) {
		return NULL;
	}

	Slang::ComPtr<IBlob> layout_diagnostics;
	slang::ProgramLayout* program_layout = linked_program->getLayout(0, layout_diagnostics.writeRef());
	log_diagnostics("Slang reflection diagnostics:\n", layout_diagnostics.get());

	if(!program_layout) {
		return NULL;
	}

	slang_reflect_context rctx = { };
	rctx.arena = arena;
	rctx.program_layout = program_layout;

	_slang_reflect_all(&rctx);

	return materialize_reflection(arena, rctx.blocks, rctx.samplers, rctx.inputs);
}

} // namespace

static_assert(SPOOPY_OPTIMIZATION_LEVEL_NONE == (int)SLANG_OPTIMIZATION_LEVEL_NONE, "");
static_assert(SPOOPY_OPTIMIZATION_LEVEL_DEFAULT == (int)SLANG_OPTIMIZATION_LEVEL_DEFAULT, "");
static_assert(SPOOPY_OPTIMIZATION_LEVEL_HIGH == (int)SLANG_OPTIMIZATION_LEVEL_HIGH, "");
static_assert(SPOOPY_OPTIMIZATION_LEVEL_MAXIMAL == (int)SLANG_OPTIMIZATION_LEVEL_MAXIMAL, "");

bool _slang_init_compiler(void) {
	static bool logged_profiles = false;

	if(global_context.global_session) {
		return true;
	}

	SlangGlobalSessionDesc desc = { };
	desc.structureSize = sizeof(SlangGlobalSessionDesc);
	desc.apiVersion = SLANG_API_VERSION;
	desc.minLanguageVersion = SLANG_LANGUAGE_VERSION_2025;
	desc.enableGLSL = false;

	SlangResult result = createGlobalSession(&desc, global_context.global_session.writeRef());
	if(SLANG_FAILED(result) || !global_context.global_session) {
		SPOOPY_LOG_ERROR("Failed to create global Slang session: %d", result);
		return false;
	}

	if(!logged_profiles) {
		log_profile_candidates(global_context.global_session.get(), "metal", {
			"metallib_3_1", "metallib_3_0",
			"metallib_2_4", "metallib_2_3", "metallib_2_2", "metallib_2_1", "metallib_2_0",
			"metal",
			"metal_3_1", "metal_3_0",
			"metal_2_4", "metal_2_3", "metal_2_2", "metal_2_1", "metal_2_0",
			"msl",
			"msl_3_1", "msl_3_0",
			"msl_2_4", "msl_2_3", "msl_2_2", "msl_2_1", "msl_2_0"
		});
		logged_profiles = true;
	}

	return true;
}

void _slang_shutdown_compiler(void) {
	global_context.global_session = nullptr;
}

bool _slang_shader_supported(const spoopy_shader_source_t* source, spoopy_transpile_options_t* transpile_opts) {
	return resolve_supported_target(source, transpile_opts, NULL);
}

bool _slang_compile(
	spoopy_shader_source_t* source,
	spoopy_shader_source_t* target,
	spoopy_transpile_options_t* transpile_opts,
	spoopy_mem_arena_t* arena
) {
	char* module_source = NULL;
	char* output_code = NULL;
	const char* module_name = NULL;
	const char* source_name = NULL;
	slang::IComponentType* components[2] = { NULL, NULL };
	Slang::ComPtr<ISession> session;
	Slang::ComPtr<IBlob> module_diagnostics;
	Slang::ComPtr<IModule> module;
	Slang::ComPtr<IEntryPoint> entry_point;
	Slang::ComPtr<IBlob> composite_diagnostics;
	Slang::ComPtr<IComponentType> composed;
	Slang::ComPtr<IBlob> link_diagnostics;
	Slang::ComPtr<IComponentType> linked;
	Slang::ComPtr<IBlob> code_diagnostics;
	Slang::ComPtr<IBlob> code_blob;

	memset(target, 0, sizeof(*target));

	target_profile profile = { SPOOPY_RENDERER_API_UNSURE, SLANG_TARGET_UNKNOWN, SLANG_PROFILE_UNKNOWN };
	if(!resolve_supported_target(source, transpile_opts, &profile)) {
		SPOOPY_LOG_ERROR("Unsupported renderer target: %u", (unsigned)source->target);
		return false;
	}

	slang::TargetDesc target_desc = { };
	target_desc.format = profile.target;
	target_desc.profile = profile.profile;
	configure_target_desc(target_desc, profile.target);

	tinystl::vector<slang::CompilerOptionEntry> compiler_options;
	spoopy_optimization_level_t optimization_level = SPOOPY_OPTIMIZATION_LEVEL_NONE;
	if(transpile_opts) {
		optimization_level = transpile_opts->compile.optimization_level;
	}

	add_int_compiler_option(
		compiler_options,
		slang::CompilerOptionName::Optimization,
		(int32_t)map_optimization_level(optimization_level)
	);
	add_int_compiler_option(compiler_options, slang::CompilerOptionName::NoMangle, 1);

	if(profile.target != SLANG_METAL) {
		add_int_compiler_option(compiler_options, slang::CompilerOptionName::GenerateWholeProgram, 1);
		add_int_compiler_option(compiler_options, slang::CompilerOptionName::PreserveParameters, 1);
	}

	target_desc.compilerOptionEntries = compiler_options.data();
	target_desc.compilerOptionEntryCount = (uint32_t)compiler_options.size();

	slang::SessionDesc session_desc = { };
	session_desc.targets = &target_desc;
	session_desc.targetCount = 1;
	session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
	session_desc.allowGLSLSyntax = true;

	SlangResult result = global_context.global_session->createSession(session_desc, session.writeRef());
	if(SLANG_FAILED(result) || !session) {
		SPOOPY_LOG_ERROR("Failed to create Slang session: %d", result);
		return false;
	}

	module_source = build_source_with_macros(source, transpile_opts);
	if(!module_source) {
		SPOOPY_LOG_ERROR("Failed to allocate shader source buffer");
		return false;
	}

	module_name = source->module_name ? source->module_name : "shader";
	source_name = (transpile_opts && transpile_opts->filename) ? transpile_opts->filename : "<embedded>";

	module = session->loadModuleFromSourceString(
		module_name,
		source_name,
		module_source,
		module_diagnostics.writeRef()
	);
	log_diagnostics("Slang module diagnostics:\n", module_diagnostics.get());

	if(!module) {
		SPOOPY_LOG_ERROR("Failed to load Slang module '%s'", module_name);
		goto fail;
	}

	spoopy_heap_free(module_source);
	module_source = NULL;

	result = module->findEntryPointByName(source->entry_point, entry_point.writeRef());
	if(SLANG_FAILED(result) || !entry_point) {
		SPOOPY_LOG_ERROR("Failed to find entry point '%s'", source->entry_point);
		goto fail;
	}

	components[0] = module.get();
	components[1] = entry_point.get();

	result = session->createCompositeComponentType(
		components,
		2,
		composed.writeRef(),
		composite_diagnostics.writeRef()
	);
	log_diagnostics("Slang composite diagnostics:\n", composite_diagnostics.get());

	if(SLANG_FAILED(result) || !composed) {
		SPOOPY_LOG_ERROR("Failed to compose Slang shader program");
		goto fail;
	}

	result = composed->link(linked.writeRef(), link_diagnostics.writeRef());
	log_diagnostics("Slang link diagnostics:\n", link_diagnostics.get());

	if(SLANG_FAILED(result) || !linked) {
		SPOOPY_LOG_ERROR("Failed to link Slang shader program");
		goto fail;
	}

	result = linked->getEntryPointCode(0, 0, code_blob.writeRef(), code_diagnostics.writeRef());
	log_diagnostics("Slang codegen diagnostics:\n", code_diagnostics.get());

	if(SLANG_FAILED(result) || !code_blob) {
		SPOOPY_LOG_ERROR("Failed to generate shader code for '%s'", source->entry_point);
		goto fail;
	}

	output_code = arena_memdup_string(arena, code_blob->getBufferPointer(), code_blob->getBufferSize());
	if(!output_code) {
		SPOOPY_LOG_ERROR("Failed to allocate transpiled shader output");
		goto fail;
	}

	target->content = output_code;
	target->content_size = code_blob->getBufferSize();
	target->entry_point = spoopy_arena_strdup(arena, source->entry_point);
	target->module_name = source->module_name ? spoopy_arena_strdup(arena, source->module_name) : NULL;
	target->stage = source->stage;
	target->target = source->target;
	target->reflection = build_reflection(arena, linked.get());

	if(!target->entry_point || !target->reflection) {
		SPOOPY_LOG_ERROR("Failed to materialize transpiled shader metadata");
		goto fail;
	}

	return true;

fail:
	if(module_source) {
		spoopy_heap_free(module_source);
	}
	memset(target, 0, sizeof(*target));
	return false;
}
