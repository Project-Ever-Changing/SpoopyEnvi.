#include <spoopy_api.h>

#if SPOOPY_HAS_INCLUDE("../../spoopy_system_info.h")
#include "../../spoopy_system_info.h"
#endif

#include <spoopy_backend.h>
#include <spoopy_graphics.h>

#include <SDL3/SDL.h>

static struct {
	spoopy_vec2_vec_int_t fs_modes;

	SDL_Mutex* display_mutex;
	SDL_DisplayID* cached_displays;
	SDL_Window* primary_window; // TODO (Multi-Window): Keep this
	// TODO (Multi-Window): Have a `spoopy_window_data_t* windows` array that uses SDL_WindowID as indexes (kinda like a hash map)
	// Also, have a `SDL_Window window_prop_cache` to store properties
	// TODO (Mutli-Window): Have `spoopy_content_scale_aspect` be part of `spoopy_window_data_t`
	spoopy_graphics_t* graphics; // TODO (Mutli-Window): Move this to `spoopy_window_data_t`
#if defined(__APPLE__)
	void* primary_view; // TODO (Multi-Window): Keep this
#endif

	// 8-byte types
	double scaling_factor; // TODO (Mutli-Window): Move this to `spoopy_window_data_t`

	SDL_AtomicInt should_quit;
	int32_t cached_display_count;
	bool initialized;
} app = { 0 };

static spoopy_video_cap_state_t (*video_query_capability)(spoopy_video_cap_t cap);

static inline SDL_DisplayID get_cached_display_safe(int32_t index) {
	if(index < 0 || index >= app.cached_display_count) {
		SPOOPY_LOG_WARN("Display index %i out of bounds (count: %i)", index, app.cached_display_count);
		return 0;
	}
	return app.cached_displays[index];
}

SPOOPY_ATTR_UNUSED static inline spoopy_vec2_int_t coords_pixels_to_screen(spoopy_vec2_int_t pixel_ofs) {
	spoopy_vec2_int_t screen_ofs;
	screen_ofs.x = round(pixel_ofs.x / app.scaling_factor);
	screen_ofs.y = round(pixel_ofs.y / app.scaling_factor);
	return screen_ofs;
}

static void video_add_mode_dpi_aware(spoopy_vec2_vec_int_t* vec_vec, spoopy_vec2_int_t screen, spoopy_vec2_int_t min_screen, spoopy_vec2_int_t max_screen) {
	spoopy_vec2_int_t pix_screen = coords_pixels_to_screen(screen);

	// The explaination provided makes sense:
	// https://github.com/taisei-project/taisei/blob/master/src/video.c
	spoopy_vec2_vec_int_add_if_bounded(vec_vec, pix_screen, min_screen, max_screen);
	spoopy_vec2_vec_int_add_if_bounded(vec_vec, screen, min_screen, max_screen);
}

static int video_compare_vec2(const void* a, const void* b) {
	const spoopy_vec2_int_t* va = a;
	const spoopy_vec2_int_t* vb = b;
	return va->w * va->h - vb->w * vb->h;
}

static spoopy_video_cap_state_t video_query_capability_generic(spoopy_video_cap_t cap) {
	switch(cap) {
		case SPOOPY_VIDEO_CAP_FULLSCREEN:
			return SPOOPY_VIDEO_CAP_STATE_AVAILABLE;
		case SPOOPY_VIDEO_CAP_EXTERNAL_RESIZE:
			return spoopy_api_window_is_fullscreen()
				? SPOOPY_VIDEO_CAP_STATE_UNAVAILABLE
				: SPOOPY_VIDEO_CAP_STATE_AVAILABLE;
	}

	SPOOPY_UNREACHABLE();
}

static void internal_init(void) {
	spoopy_global_context_init();
	_backend_funcs.init();

	// TODO (States): Have `draw` state logic be initialized here
}

static void video_init_sdl(void) {
	SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0", SDL_HINT_OVERRIDE);

	if(!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		SPOOPY_LOG_ERROR("SDL_InitSubSystem() - ERROR: %s\n", SDL_GetError());
	}
}

// TODO (Multi-Window): Have window index, that straightforward
static spoopy_vec2_int_t video_get_screen_framebuffer_size(void) {
	spoopy_vec2_int_t size;
	SDL_GetWindowSizeInPixels(app.primary_window, &size.w, &size.h);
	return size;
}

// TODO (Mutli-Window): We need a window index..
// TODO (Events): Have a update mode lists event for Spoopy
static void video_update_mode_lists(void) {
	bool fullscreen_available = false;

	SDL_LockMutex(app.display_mutex);

	spoopy_vec2_vec_int_resize(&app.fs_modes, 16);

	spoopy_vec2_int_t screenspace_min_size = (spoopy_vec2_int_t) { 0 };
	SDL_GetWindowMinimumSize(app.primary_window, &screenspace_min_size.x, &screenspace_min_size.y);
	coords_pixels_to_screen(screenspace_min_size);

	for(int i=0; i<spoopy_api_get_screen_count(); ++i) {
		SDL_DisplayID display = get_cached_display_safe(i);
		SPOOPY_LOG_INFO("Found display #%i: %s", i, spoopy_api_get_screen_name(i));

		const SDL_DisplayMode* desktop_mode;
		spoopy_vec2_int_t screenspace_max_size = {};

		if(!(desktop_mode = SDL_GetDesktopDisplayMode(display))) {
			SPOOPY_LOG_WARN("SDL_GetDesktopDisplayMode() - WARN: %s\n", SDL_GetError());
		}else {
#ifdef SPOOPY_BUILD_DEBUG
			SPOOPY_LOG_INFO("Desktop mode: %ix%i @ %.2f Hz, scale: %.2f",
				desktop_mode->w, desktop_mode->h,
				desktop_mode->refresh_rate, desktop_mode->pixel_density
			);
#endif

			screenspace_max_size.w = desktop_mode->w;
			screenspace_max_size.h = desktop_mode->h;
		}

		int mcount;
		SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &mcount);

		for(int j=0; j<mcount; ++j) {
			const SDL_DisplayMode* mode = modes[j];

#ifdef SPOOPY_BUILD_DEBUG
			SPOOPY_LOG_INFO("Display mode #%i: %ix%i@%gHz; scale = %g", i,
				mode->w, mode->h, mode->refresh_rate, mode->pixel_density);
#endif

			video_add_mode_dpi_aware(&app.fs_modes, (spoopy_vec2_int_t) {{mode->w, mode->h }}, screenspace_min_size, screenspace_max_size);
			fullscreen_available = true;
		}

		SDL_free(modes);
	}

	spoopy_vec2_vec_int_compact(&app.fs_modes);
	spoopy_vec2_vec_int_qsort(&app.fs_modes, video_compare_vec2);

	if(!fullscreen_available) {
		SPOOPY_LOG_WARN("No available fullscreen modes");
	}

	SDL_UnlockMutex(app.display_mutex);
}

// TODO (Multi-Window): Have window index, that straightforward
static void video_update_scaling_factor(int width) {
	spoopy_vec2_int_t fb = video_get_screen_framebuffer_size();
	assert(fb.w > 0);

	double scaling_factor = (double)fb.w / width;
	if(scaling_factor != app.scaling_factor) {
		SPOOPY_LOG_INFO("Scaling factor updated: %f -> %f", app.scaling_factor, scaling_factor);
		app.scaling_factor = scaling_factor;
		video_update_mode_lists();
	}
}

static void new_primary_window_internal(uint32_t display, const char* title, uint32_t width, uint32_t height, spoopy_window_flags_t flags, bool fallback) {
	SDL_PropertiesID props = SDL_CreateProperties();

	if(title && *title) {
		SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
	}

	if(spoopy_graphics_get_renderer(app.graphics) & SPOOPY_RENDERER_API_METAL) {
		SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, true);
	}

	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));

	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, (flags & SPOOPY_WINDOW_FLAG_RESIZABLE) != 0);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, (flags & SPOOPY_WINDOW_FLAG_BORDERLESS) != 0);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, (flags & SPOOPY_WINDOW_FLAG_FULLSCREEN) != 0);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, (flags & SPOOPY_WINDOW_FLAG_HIGHDPI) != 0);

	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, false);

	app.primary_window = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);

	if(app.primary_window) {
		SDL_ShowWindow(app.primary_window);
		spoopy_api_video_update_mode(0);
		return;
	}

	if(fallback) {
		SPOOPY_LOG_ERROR("Failed to create window on display: [%s - #%u] after fallback - ERROR: %s\n"
			, spoopy_api_get_screen_name(display)
			, display
			, SDL_GetError()
		);

		return;
	}

	return new_primary_window_internal(display, title, width, height, flags & ~SPOOPY_WINDOW_FLAG_FULLSCREEN, true);
}

static void new_primary_window(const char* title, spoopy_window_flags_t flags, const spoopy_rec_int_t* p_rect) {
	const float scale = spoopy_api_get_screen_max_scale();

	int32_t r_screen = spoopy_api_get_screen_from_rect(p_rect);
	if(r_screen < 0) {
		r_screen = SPOOPY_PRIMARY_SCREEN_INDEX;
	}

	const int win_w_pts = spoopy_max(1, (int)SDL_lround(p_rect->size.w / scale));
	const int win_h_pts = spoopy_max(1, (int)SDL_lround(p_rect->size.h / scale));
	new_primary_window_internal(r_screen, title, (uint32_t)win_w_pts, (uint32_t)win_h_pts, flags, false);

#if defined(__APPLE__)
	if(app.primary_window) {
		app.primary_view = SDL_Metal_CreateView(app.primary_window);
		assert(app.primary_view != NULL);
		spoopy_graphics_set_mode(app.graphics, app.primary_view);
	}
#endif

	SPOOPY_LOG_INFO("Create a new window: %ix%i, on display #%i %s\n", win_w_pts, win_h_pts, r_screen, spoopy_api_get_screen_name(r_screen));
	SDL_RaiseWindow(app.primary_window);
}

bool spoopy_api_should_quit(void) {
	return SDL_GetAtomicInt(&app.should_quit);
}

void spoopy_api_request_quit(void) {
	if(SDL_CompareAndSwapAtomicInt(&app.should_quit, 0, 1)) {
		SPOOPY_LOG_INFO("Quit Requested");
	}
}

int32_t spoopy_api_get_screen_count(void) {
	return app.cached_display_count;
}

void spoopy_api_refresh_screens(void) {
	SDL_LockMutex(app.display_mutex);
	SDL_free(app.cached_displays);

	int screen_count = 0;
	if(!(app.cached_displays = SDL_GetDisplays(&screen_count))) {
		SPOOPY_LOG_ERROR("SDL_InitSubSystem() - ERROR: %s\n", SDL_GetError());
		app.cached_display_count = 0;
	} else {
		app.cached_display_count = screen_count;
	}

	SDL_UnlockMutex(app.display_mutex);
}

void spoopy_api_video_update_mode(uint32_t window_index) {
	(void)window_index;

	// TODO (Events): Implement vsync spoopy event for users
	// spoopy_update_event_vsync();

	int width;
	SDL_GetWindowSize(app.primary_window, &width, NULL);

	video_update_scaling_factor(width);
	// TODO (Viewport): Have a `_window_update_viewport` function
	// TODO (Swapchain): Have `_backend_funcs.framebuffer_update_all` to update framebuffer + swapchain
}

bool spoopy_api_window_is_fullscreen(void) {
	return SDL_GetWindowFlags(app.primary_window) & SDL_WINDOW_FULLSCREEN;
}

bool spoopy_api_window_is_resizable(void) {
	return SDL_GetWindowFlags(app.primary_window) & SDL_WINDOW_RESIZABLE;
}

void spoopy_api_video_init(const spoopy_video_init_params_t* params) {
	if(app.initialized) {
		SPOOPY_LOG_WARN("`spoopy_api_video_init()` has already been called!");
		return;
	}

	video_init_sdl();

	const char *driver = SDL_GetCurrentVideoDriver();
	SPOOPY_LOG_INFO("Using driver '%s'", driver);

	video_query_capability = video_query_capability_generic;


	app.initialized = true;
	app.display_mutex = SDL_CreateMutex();
	spoopy_vec2_vec_int_init(&app.fs_modes, 16);

	app.scaling_factor = 0;

	internal_init();

	uint32_t w = spoopy_max(params->width, 1u);
	uint32_t h = spoopy_max(params->height, 1u);

	app.graphics = spoopy_graphics_new(params->renderer);
	spoopy_api_refresh_screens();

	spoopy_rec_int_t window_screen = (spoopy_rec_int_t){
		.point = { .x = 0, .y = 0 },
		.size  = {
			.w = w,
			.h = h
		}
	};

	new_primary_window(params->title, params->flags, &window_screen);
	// TODO (Set Mode): Include `set_mode` API function here
}

// TODO (Framework):
// This allows us to create our own file loading system even for other platforms later on.
// I mean, we could stretch the meaning of "Spoopy Renderer" to say it includes image loading since
// textures are a big part of rendering, which requires image loading
// Most will see through that bullshit anyway, but I don't think anyone will even care
bool spoopy_api_image_load_from_file(const char* path, spoopy_image_file_format_t file_format, spoopy_image_t* dst) {
	(void)path;
	(void)file_format;
	(void)dst;

	// For now though, we'll just have this be nothing
	return false;
}

void spoopy_api_video_shutdown(void) {
	if(!app.initialized) {
		return;
	}

	_backend_funcs.shutdown();
	spoopy_shader_cleanup();

#if defined(__APPLE__)
	SDL_Metal_DestroyView(app.primary_view);
#endif

	SDL_DestroyWindow(app.primary_window);

	SDL_LockMutex(app.display_mutex);
	SDL_free(app.cached_displays);
	app.cached_display_count = 0;
	SDL_UnlockMutex(app.display_mutex);

	spoopy_vec2_vec_int_destroy(&app.fs_modes);
	SDL_DestroyMutex(app.display_mutex);
	spoopy_heap_free(app.graphics);
	SDL_SetAtomicInt(&app.should_quit, 0);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	app.initialized = false;
}

int32_t spoopy_api_get_screen_from_rect(const spoopy_rec_int_t* rect) {
	const SDL_Rect r = {
		.x = rect->point.x,
		.y = rect->point.y,
		.w = rect->size.w,
		.h = rect->size.h
	};

	int32_t nearest_area = 0;
	int32_t pos_screen = -1;

	SDL_LockMutex(app.display_mutex);

	SDL_DisplayID* displays = app.cached_displays;

	for(int32_t i=0; i<spoopy_api_get_screen_count(); i++) {
		SDL_Rect db;
		if(!SDL_GetDisplayBounds(displays[i], &db)) {
			continue;
		}

		SDL_Rect inter;
		if(SDL_GetRectIntersection(&db, &r, &inter)) {
			const int area = inter.w * inter.h;
			if(area > nearest_area) {
				pos_screen = i;
				nearest_area = area;
			}
		}
	}

	SDL_UnlockMutex(app.display_mutex);
	return pos_screen;
}

const char* spoopy_api_get_screen_name(uint32_t screen_index) {
	SDL_LockMutex(app.display_mutex);
	SDL_DisplayID display = get_cached_display_safe(screen_index);
	const char* name = SDL_GetDisplayName(display);

	if(name == NULL) {
		SPOOPY_LOG_WARN("SDL_GetDisplayName() - WARN: %s\n", SDL_GetError());
		name = "Unknown";
	}

	SDL_UnlockMutex(app.display_mutex);
	return name;
}

float spoopy_api_get_screen_max_scale(void) {
	SDL_LockMutex(app.display_mutex);

	int32_t count = app.cached_display_count;
	SDL_DisplayID* displays = app.cached_displays;
	float max_scale = 1.0f;

	if(!displays || count <= 0) {
		goto return_max_scale;
	}

	for(int32_t i=0; i<count; i++) {
		const float s = SDL_GetDisplayContentScale(displays[i]);
		if(s > max_scale) {
			max_scale = s;
		}
	}

return_max_scale:
	SDL_UnlockMutex(app.display_mutex);
	return max_scale;
}

spoopy_rec_int_t spoopy_api_screen_get_usable_rect(int32_t screen_index) {
	SDL_LockMutex(app.display_mutex);

	spoopy_rec_int_t rec2 = { 0 };

	if(screen_index < 0) {
		screen_index = SPOOPY_PRIMARY_SCREEN_INDEX;
	}

	SDL_DisplayID display = get_cached_display_safe(screen_index);
	if(display == 0) {
		goto got_usable_rect;
	}
	SDL_Rect sdl_rec2 = { 0 };

	if(!SDL_GetDisplayUsableBounds(display, &sdl_rec2)) {
		goto got_usable_rect;
	}

	spoopy_vec2_int_t pos = {  .x = sdl_rec2.x, .y = sdl_rec2.y };
	spoopy_vec2_int_t size = { .x = sdl_rec2.w, .y = sdl_rec2.h };
	rec2 = (spoopy_rec_int_t){ .point = pos, .size = size };

got_usable_rect:
	SDL_UnlockMutex(app.display_mutex);
	return rec2;
}

spoopy_renderer_t spoopy_api_window_get_renderer(void) {
	return spoopy_graphics_get_renderer(app.graphics);
}

void* spoopy_api_window_get_native_handle(void) {
	return app.primary_window;
}

void* spoopy_api_window_get_native_drawable(void) {
	return spoopy_graphics_get_native_drawable(app.graphics);
}

void spoopy_api_clear(spoopy_buffer_kind_t flags, const spoopy_color_t* color_val, float depth_val) {
	_backend_funcs.clear(app.graphics, flags, color_val, depth_val);
}
