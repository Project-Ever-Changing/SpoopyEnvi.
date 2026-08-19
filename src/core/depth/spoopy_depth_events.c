#include <spoopy_api.h>
#include <SDL3/SDL.h>

#if SPOOPY_HAS_INCLUDE("spoopy_system_info.h")
#include "spoopy_system_info.h"
#endif


// In the future, I'm probably going to make this multithreaded
// but for now, this is fine
// Plus! I want to have threadsafety and thread management
// via a higher level languages since
// for better safety nets + easier to work with
// when it comes to threads
// Definitely not worth the effort to make this multithreaded in C

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

uint32_t sdl_first_user_event;

static bool spoopy_events_handler_quit(SDL_Event *event, void *arg);
static bool spoopy_events_handle_video(SDL_Event *event, void *arg);

static const spoopy_event_handler_t default_handlers[] = {
	{ .proc = spoopy_events_handler_quit, .priority = EPRIO_SYSTEM, .event_type = SDL_EVENT_QUIT },
	{ .proc = spoopy_events_handle_video, .priority = EPRIO_SYSTEM },
	{ .proc = NULL, .priority = 0, .event_type = 0 }
};

static spoopy_event_handler_t* spoopy_events_register_default_handlers(spoopy_event_handler_t* h);

static inline int prio_index(EventPriority prio) {
	return prio - EPRIO_FIRST;
}

spoopy_event_handler_t* spoopy_events_register_handlers(spoopy_event_handler_t* handler_ptr, uint32_t capacity, spoopy_event_handler_t handlers[capacity]) {
	const size_t default_count = ARRAY_SIZE(default_handlers) - 1; // Exclude null terminator
	const size_t total_count = default_count + capacity + 1; // +1 for null terminator

	handler_ptr = spoopy_heap_realloc(handler_ptr, total_count * sizeof(spoopy_event_handler_t));
	memcpy(handler_ptr, default_handlers, default_count * sizeof(spoopy_event_handler_t));

	if(handlers) {
		memcpy(handler_ptr + default_count, handlers, capacity * sizeof(spoopy_event_handler_t));
	}

	// Null terminate the handler array
	handler_ptr[default_count + capacity] = (spoopy_event_handler_t){0};

	uint32_t pcount[NUM_EPRIOS] = { 0 };

	for(spoopy_event_handler_t* h = handler_ptr; h->proc; ++h) {
		++pcount[prio_index(h->priority)];
	}

	for(uint32_t i=0, pos=0; i<NUM_EPRIOS; ++i) {
		uint32_t count = pcount[i];
		pcount[i] = pos;
		pos += count;
	}

	spoopy_event_handler_t temp[total_count];
	memcpy(temp, handler_ptr, total_count * sizeof(spoopy_event_handler_t));

	for(uint32_t i=0; i<total_count && temp[i].proc; ++i) {
		handler_ptr[pcount[prio_index(temp[i].priority)]++] = temp[i];
	}

	return handler_ptr;
}

void spoopy_events_init(int32_t NUM_USER_EVENTS, spoopy_event_handler_t** handler_ptr) {
	if(!handler_ptr) {
		SPOOPY_LOG_ERROR("handler_ptr is NULL, cannot initialize events without a proper pointer.");
		return;
	}

	if(!SDL_Init(SDL_INIT_EVENTS)) {
		SPOOPY_LOG_ERROR("SDL_Init(SDL_INIT_EVENTS) failed: %s", SDL_GetError());
	}

	sdl_first_user_event = SDL_RegisterEvents(NUM_USER_EVENTS);
	if(sdl_first_user_event == ((uint32_t)-1)) {
		SPOOPY_LOG_ERROR(
			"You have reached the maximum number of user events supported by SDL."
			"Somewhere in your code, you might of had a buffer overflow or memory corruption."
			"Or, you some how have 4294967295 user events registered, if so, congrats!"
		);
	}


	*handler_ptr = spoopy_events_register_default_handlers(*handler_ptr);
}


/* =============================================================================
 * Default Handlers
 * ============================================================================= */

static spoopy_event_handler_t* spoopy_events_register_default_handlers(spoopy_event_handler_t* h) {
	return spoopy_events_register_handlers(h, 0, NULL);
}

static bool spoopy_events_handler_quit(SDL_Event *event, void *arg) {
	(void)event;
	(void)arg;
	spoopy_api_request_quit();
	return true;
}

static bool spoopy_events_handle_video(SDL_Event *event, void *arg) {
	(void)arg;

	switch(event->type) {
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
#ifdef SPOOPY_BUILD_DEBUG
			SPOOPY_LOG_INFO("Window pixel size changed: %ux%u", event->window.data1, event->window.data2);
#endif

			spoopy_api_video_update_mode(0);
			break;
		case SDL_EVENT_DISPLAY_ADDED:
		case SDL_EVENT_DISPLAY_REMOVED:
			spoopy_api_refresh_screens();
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			// TODO (Events): User supported focus event is required
			break;
		default:
			// TODO (Events): This is where the spoopy event system takes place
			break;
	}

	return false;
}


static bool spoopy_events_invoke_handler(SDL_Event *event, spoopy_event_handler_t *handler) {
	assert(handler->proc != NULL);

	if(!handler->event_type || (uint32_t)handler->event_type == (uint32_t)event->type) {
		return handler->proc(event, handler->arg);
	}

	return false;
}

void spoopy_events_poll(spoopy_event_handler_t* handlers, EventFlags flags) {
	for(;;) {
		if(!(flags & EVENT_FLAG_NOPUMP)) {
			SDL_PumpEvents();
		}

		SDL_Event events[8];
		int n_events = SDL_PeepEvents(events, ARRAY_SIZE(events), SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);

		if(SPOOPY_UNLIKELY(n_events < 0)) {
			SPOOPY_LOG_ERROR("SDL_PeepEvents failed: %s", SDL_GetError());
		}

		if(n_events == 0) {
			break;
		}

		for(SDL_Event *e = events, *end = events + n_events; e < end; ++e) {
			for(spoopy_event_handler_t *h = handlers; h->proc; ++h) {
				if(spoopy_events_invoke_handler(e, h)) {
					SPOOPY_LOG_INFO("Event type=%d handled by handler", e->type);
					break;
				}
			}
		}
	}
}
