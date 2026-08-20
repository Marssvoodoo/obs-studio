/******************************************************************************
 * OBS Studio - optimized media copy helpers
 ******************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void copy_video_plane_optimized(uint8_t *dst, const uint8_t *src, uint32_t width, uint32_t height, uint32_t dst_stride,
				uint32_t src_stride);

#ifdef __cplusplus
}
#endif
