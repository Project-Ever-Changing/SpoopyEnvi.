#pragma once

// For more general API functions for the user to use.
// Everything here is meant to be used by the developer

#include <spoopy.h>
#include <utils/spoopy_misc_math.h>
#include <memory/spoopy_memory.h>
#include <memory/spoopy_arena.h>
#include <memory/spoopy_vector.h>
#include <format/spoopy_fileformats.h>
#include <scene/3d/spoopy_camera_3d.h>
#include <audio/spoopy_mixer.h>
#include <audio/spoopy_audio.h>
#include <spoopy_shader.h>
#include <spoopy_thread.h>
#include <spoopy_log.h>
#include <spoopy_vertex_attr.h>
#include <spoopy_events.h>
#include <spoopy_color.h>
#include <spoopy_image_mths.h>
#include <spoopy_time.h>
#include <spoopy_framerate.h>

#include <spoopy_types.h>

#ifdef __cplusplus
extern "C" {
#endif

SPOOPY_FUNC_CORE spoopy_pipeline_t* spoopy_api_pipeline_link(uint32_t num_objs, spoopy_shader_object_t* objs[]);
SPOOPY_FUNC_CORE void spoopy_api_pipeline_compile(spoopy_pipeline_t* pipeline, uint32_t spec_count, spoopy_vertex_attr_spec_t spec[spec_count], uint32_t buffer_index);
SPOOPY_FUNC_CORE void spoopy_api_pipeline_bind(spoopy_pipeline_t* pipeline);
SPOOPY_FUNC_CORE bool spoopy_api_shader_init(spoopy_shader_object_t* shader, spoopy_shader_source_t* info);
SPOOPY_FUNC_CORE bool spoopy_api_vertex_buffer_create(spoopy_vertex_buffer_t* buffer, uint32_t capacity, uint32_t count, void* data, uint32_t stride);
SPOOPY_FUNC_CORE bool spoopy_api_index_buffer_create(spoopy_index_buffer_t* buffer, uint32_t count, void* data);
SPOOPY_FUNC_CORE void spoopy_api_shader_destroy(spoopy_shader_object_t* shader, bool must_destroy);
SPOOPY_FUNC_CORE void spoopy_api_add_macro(spoopy_transpile_options_t* options, const char* name, const char* value);
SPOOPY_FUNC_CORE bool spoopy_api_shader_supported(const spoopy_shader_source_t* info, spoopy_transpile_options_t* transpile_opts);
//
// TODO (Multi-Window): Have a parameter `window_index` to get it for each window
SPOOPY_FUNC_CORE void spoopy_api_clear(spoopy_buffer_kind_t flags, const spoopy_color_t* color_val, float depth_val);
SPOOPY_FUNC_CORE void spoopy_api_draw_mesh(const spoopy_mesh_t* mesh, spoopy_pipeline_t* pipeline);
SPOOPY_FUNC_CORE void spoopy_api_swap_buffers(void);
SPOOPY_FUNC_CORE size_t spoopy_api_texture_size(void);
SPOOPY_FUNC_CORE void spoopy_api_texture_create(spoopy_texture_t* tex, const spoopy_texture_params_t* p);
SPOOPY_FUNC_CORE void spoopy_api_texture_get_size(const spoopy_texture_params_t params, uint32_t mipmap, uint32_t* width, uint32_t* height);
SPOOPY_FUNC_CORE void spoopy_api_texture_fill(spoopy_texture_t* tex, uint32_t mipmap, uint32_t layer, const spoopy_image_t* img);
SPOOPY_FUNC_CORE void spoopy_api_texture_set(spoopy_pipeline_t* pipeline, const char* uniform_name, spoopy_texture_t* tex);
SPOOPY_FUNC_CORE void spoopy_api_texture_destroy(spoopy_texture_t* tex);
SPOOPY_FUNC_CORE size_t spoopy_api_buffer_size(spoopy_buffer_type_t type);
SPOOPY_FUNC_CORE void spoopy_api_video_init(const spoopy_video_init_params_t* params);
SPOOPY_FUNC_CORE void spoopy_api_video_update_mode(uint32_t window_index);
SPOOPY_FUNC_CORE void spoopy_api_video_shutdown(void);

SPOOPY_FUNC_CORE int32_t spoopy_api_get_screen_count(void);
SPOOPY_FUNC_CORE int32_t spoopy_api_get_screen_from_rect(const spoopy_rec_int_t* rect);
SPOOPY_FUNC_CORE const char* spoopy_api_get_screen_name(uint32_t screen_index);
SPOOPY_FUNC_CORE float spoopy_api_get_screen_max_scale(void);
SPOOPY_FUNC_CORE spoopy_rec_int_t spoopy_api_screen_get_usable_rect(int32_t screen_index);
SPOOPY_FUNC_CORE void spoopy_api_refresh_screens(void);

SPOOPY_FUNC_CORE spoopy_capability_bits_t spoopy_api_capabilities_current(spoopy_pipeline_t* pipeline);
SPOOPY_FUNC_CORE spoopy_capability_bits_t spoopy_capability_bit(spoopy_render_capability_t cap);
SPOOPY_FUNC_CORE void spoopy_api_capability(spoopy_pipeline_t* pipeline, spoopy_render_capability_t cap, bool value);
SPOOPY_FUNC_CORE void spoopy_api_enable(spoopy_pipeline_t* pipeline, spoopy_render_capability_t cap);
SPOOPY_FUNC_CORE void spoopy_api_disable(spoopy_pipeline_t* pipeline, spoopy_render_capability_t cap);

SPOOPY_FUNC_CORE void spoopy_api_blend(spoopy_pipeline_t* pipeline, spoopy_blend_mode_t mode);
SPOOPY_FUNC_CORE spoopy_blend_mode_t spoopy_api_blend_current(spoopy_pipeline_t* pipeline);

SPOOPY_FUNC_CORE void spoopy_api_cull(spoopy_pipeline_t* pipeline, spoopy_cull_face_mode_t mode);
SPOOPY_FUNC_CORE spoopy_cull_face_mode_t spoopy_api_cull_current(spoopy_pipeline_t* pipeline);

// TODO (Multi-Window): Have a parameter `window_index` to get it for each window
SPOOPY_FUNC_CORE spoopy_renderer_t spoopy_api_window_get_renderer(void);
SPOOPY_FUNC_CORE void* spoopy_api_window_get_native_handle(void);
SPOOPY_FUNC_CORE void* spoopy_api_window_get_native_drawable(void);

// TODO (Multi-Window): Have a parameter `window_index` to get it for each window
SPOOPY_FUNC_CORE bool spoopy_api_window_is_fullscreen(void);
SPOOPY_FUNC_CORE bool spoopy_api_window_is_resizable(void);
// SPOOPY_FUNC_CORE void spoopy_api_window_get_viewport(void);
// SPOOPY_FUNC_CORE spoopy_vec2_int_t spoopy_api_window_get_framebuffer_size(uint32_t screen_index);
// SPOOPY_FUNC_CORE bool spoopy_api_window_fullscreen_toggle(spoopy_window_t window);
// SPOOPY_FUNC_CORE void spoopy_api_window_set_fullscreen(spoopy_window_t window, bool fullscreen);
// SPOOPY_FUNC_CORE void spoopy_api_window_set_resizeable(spoopy_window_t window, bool resizeable);
// SPOOPY_FUNC_CORE void spoopy_api_window_destroy(spoopy_window_t window);


// NOTE: No uniforms are being cached currently, so repeated lookups will be slower
// I have no plans to cache them at the moment, or if at all, so be aware of this
// However, it would be genuinely better to have you, the developer, cache them yourself
// if you plan to use them multiple times
SPOOPY_FUNC_CORE spoopy_uniform_t* spoopy_api_shader_uniform(spoopy_pipeline_t* pipeline, const char* name);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_int(spoopy_uniform_t* uniform, int value);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_int2(spoopy_uniform_t* uniform, int value0, int value1);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_int3(spoopy_uniform_t* uniform, int value0, int value1, int value2);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_int4(spoopy_uniform_t* uniform, int value0, int value1, int value2, int value3);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_ints(spoopy_uniform_t* uniform, const int* values, int count);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_float(spoopy_uniform_t* uniform, float value);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_float2(spoopy_uniform_t* uniform, float value0, float value1);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_float3(spoopy_uniform_t* uniform, float value0, float value1, float value2);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_float4(spoopy_uniform_t* uniform, float value0, float value1, float value2, float value3);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_floats(spoopy_uniform_t* uniform, const float* values, int count);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_bool(spoopy_uniform_t* uniform, bool value);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_matrix3(spoopy_uniform_t* uniform, const float* values);
SPOOPY_FUNC_CORE void spoopy_api_uniform_set_matrix4(spoopy_uniform_t* uniform, const float* values);

SPOOPY_FUNC_CORE bool spoopy_api_image_load_from_file(const char* path, spoopy_image_file_format_t file_format, spoopy_image_t* dst);

SPOOPY_FUNC_CORE bool spoopy_api_should_quit(void);
SPOOPY_FUNC_CORE bool spoopy_api_get_config_vsync(void);
SPOOPY_FUNC_CORE void spoopy_api_request_config_vsync(bool enabled);
SPOOPY_FUNC_CORE void spoopy_api_request_quit(void);

SPOOPY_FUNC_CORE bool spoopy_api_shader_transpile(
	spoopy_shader_source_t* source,
	spoopy_shader_source_t* target,
	spoopy_transpile_options_t* transpile_opts,
	spoopy_mem_arena_t* arena
);

#ifdef __cplusplus
}
#endif
