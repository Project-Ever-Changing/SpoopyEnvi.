#pragma once

#include <spoopy_log.h>

#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns the shared Metal device (creates it on first call)
id<MTLDevice> spoopy_metal_init(void);
id<MTLCommandBuffer> spoopy_metal_command_buffer(void);
id<MTLRenderCommandEncoder> spoopy_metal_render_command_encoder(void);

#ifdef __cplusplus
}
#endif
