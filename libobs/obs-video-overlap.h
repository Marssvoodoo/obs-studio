/******************************************************************************
 * OBS Studio - overlap-safe visible-plane copy primitive
 ******************************************************************************/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void obs_copy_overlapping_video_plane(uint8_t *dst, const uint8_t *src, uint32_t width, uint32_t height,
						    uint32_t dst_stride, uint32_t src_stride, uint8_t *overlap_buffer)
{
	for (uint32_t row = 0; row < height; ++row)
		memcpy(overlap_buffer + (size_t)row * width, src + (size_t)row * src_stride, width);

	for (uint32_t row = 0; row < height; ++row)
		memcpy(dst + (size_t)row * dst_stride, overlap_buffer + (size_t)row * width, width);
}
