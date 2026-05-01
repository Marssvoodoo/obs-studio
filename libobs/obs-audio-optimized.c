/******************************************************************************
    OBS Studio - Audio Pipeline Optimizations
    
    This file contains optimized versions of performance-critical audio
    functions with SIMD intrinsics for better performance.
    
    Optimizations:
    - Vectorized audio mixing (SSE2/AVX)
    - Prefetching for better cache utilization
    - Aligned memory access paths
******************************************************************************/

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "obs-internal.h"
#include "util/threading.h"
#include "util/util_uint64.h"
#include "util/sse-intrin.h"

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#define OBS_X86_SIMD 1
#else
#define OBS_X86_SIMD 0
#endif

#if OBS_X86_SIMD
#ifdef _MSC_VER
#include <intrin.h>
#include <immintrin.h>
#else
#include <cpuid.h>
#include <x86intrin.h>
#endif
#endif

#if OBS_X86_SIMD
// Check for AVX2 support at runtime, including OS support for YMM state.
static bool cpu_supports_avx2(void)
{
	static volatile long avx2_supported = -1;
	long cached = os_atomic_load_long(&avx2_supported);
	if (cached == -1) {
		long detected;
#ifdef _MSC_VER
		int info[4];
		__cpuid(info, 0);
		if (info[0] >= 7) {
			__cpuidex(info, 1, 0);
			const bool osxsave = (info[2] & (1 << 27)) != 0;
			const bool avx = (info[2] & (1 << 28)) != 0;
			unsigned long long xcr0 = 0;
			if (osxsave && avx)
				xcr0 = _xgetbv(0);
			const bool ymm_state = (xcr0 & 0x6) == 0x6;

			__cpuidex(info, 7, 0);
			detected = ymm_state && (info[1] & (1 << 5)) ? 1 : 0;
		} else {
			detected = 0;
		}
#else
		unsigned int eax, ebx, ecx, edx;
		if (__get_cpuid_max(0, NULL) >= 7) {
			__cpuid(1, eax, ebx, ecx, edx);
			const bool osxsave = (ecx & bit_OSXSAVE) != 0;
			const bool avx = (ecx & bit_AVX) != 0;
			uint64_t xcr0 = 0;
			if (osxsave && avx) {
				uint32_t xcr0_lo;
				uint32_t xcr0_hi;
				__asm__ volatile("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
				xcr0 = ((uint64_t)xcr0_hi << 32) | xcr0_lo;
			}
			const bool ymm_state = (xcr0 & 0x6) == 0x6;
			__cpuid_count(7, 0, eax, ebx, ecx, edx);
			detected = ymm_state && (ebx & bit_AVX2) ? 1 : 0;
		} else {
			detected = 0;
		}
#endif
		long expected = -1;
		if (!os_atomic_compare_exchange_long(&avx2_supported, &expected, detected))
			detected = expected;
		cached = detected;
	}
	return cached == 1;
}
#endif

/**
 * Optimized audio mixing using SSE2 intrinsics
 * Processes 4 floats at a time instead of 1
 * 
 * @param mix      Destination mix buffer (16-byte aligned preferred)
 * @param aud      Source audio buffer (16-byte aligned preferred)
 * @param count    Number of floats to mix
 */
#if OBS_X86_SIMD
static inline void mix_audio_sse2(float *mix, const float *aud, size_t count)
{
	size_t i = 0;
	const size_t simd_count = count & ~3; // Round down to multiple of 4

	// Process 4 floats at a time with SSE2
	for (i = 0; i < simd_count; i += 4) {
		// Prefetch next cache line (64 bytes ahead)
		/* Prefetch 64 floats (256 bytes / 4 cache lines) ahead. The
		 * previous +16 distance landed on the same/next cache line as
		 * the current SSE2 store, so the prefetch fired 4× per line
		 * with zero useful effect (the line was already coming in via
		 * the demand load). 64 floats stays well within reach for a
		 * single iteration's memory latency. */
		_mm_prefetch((const char *)(aud + i + 64), _MM_HINT_T0);
		_mm_prefetch((const char *)(mix + i + 64), _MM_HINT_T0);

		__m128 v_mix = _mm_loadu_ps(&mix[i]);
		__m128 v_aud = _mm_loadu_ps(&aud[i]);
		__m128 v_result = _mm_add_ps(v_mix, v_aud);
		_mm_storeu_ps(&mix[i], v_result);
	}

	// Handle remaining elements (0-3)
	for (; i < count; i++) {
		mix[i] += aud[i];
	}
}
#endif

#if OBS_X86_SIMD
/* AVX2 functions are defined in obs-audio-optimized-avx2.c, compiled
 * separately with /arch:AVX2 (MSVC) or -mavx2 (GCC/Clang). */
extern void mix_audio_avx2(float *mix, const float *aud, size_t count);
extern void zero_audio_buffer_avx2(float *buffer, size_t count);
#endif

/**
 * Optimized multi-channel audio mixing
 * This is the drop-in replacement for the hot loop in mix_audio()
 *
 * Uses SIMD instructions when available for 4-8x performance improvement
 *
 * @param mixes         Output mix buffers
 * @param mixers        Bitmask of mix slots the source is routed to;
 *                      slots not set are skipped entirely (saves 5/6 of
 *                      the per-source work for the common single-mix case)
 * @param channels      Number of active audio channels
 * @param audio_buffers Source per-mix per-channel float buffers
 * @param start_point   First sample offset within the output mix buffer
 * @param total_floats  Number of samples to mix
 */
void mix_audio_optimized(struct audio_output_data *mixes, uint32_t mixers, size_t channels,
                         float *(*audio_buffers)[MAX_AUDIO_CHANNELS],
                         size_t start_point, size_t total_floats)
{
	/* Hoist the SIMD-path decision out of the per-channel × per-mix loop.
	 * cpu_supports_avx2() is a cached atomic load + branch; doing it once
	 * per call instead of MAX_AUDIO_MIXES × channels times is a measurable
	 * win in the hot mix path. */
#if OBS_X86_SIMD
	const bool use_avx2 = cpu_supports_avx2() && total_floats >= 8;
	const bool use_sse2 = !use_avx2 && total_floats >= 4;
#endif

	for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
		if (!(mixers & (1u << mix_idx)))
			continue;

		for (size_t ch = 0; ch < channels; ch++) {
			float *mix = mixes[mix_idx].data[ch] + start_point;
			float *aud = audio_buffers[mix_idx][ch];

#if OBS_X86_SIMD
			if (use_avx2) {
				mix_audio_avx2(mix, aud, total_floats);
			} else if (use_sse2) {
				mix_audio_sse2(mix, aud, total_floats);
			} else {
				for (size_t i = 0; i < total_floats; i++)
					mix[i] += aud[i];
			}
#else
			for (size_t i = 0; i < total_floats; i++)
				mix[i] += aud[i];
#endif
		}
	}
}

/**
 * Optimized memory copy for video frames.
 *
 * For large planes (>256 KB) with a 16-byte-aligned destination, non-temporal
 * (streaming) stores are used so the write traffic bypasses the CPU cache and
 * avoids polluting it with data that will not be re-read from CPU memory.
 * When the destination is not 16-byte aligned (required by _mm_stream_si128),
 * the function safely falls back to 16-byte vectorized stores (_mm_storeu_si128)
 * which are still faster than a byte-by-byte scalar loop.
 *
 * @param dst          Destination buffer
 * @param src          Source buffer
 * @param width        Width in bytes
 * @param height       Height in lines
 * @param dst_stride   Destination stride/pitch in bytes
 * @param src_stride   Source stride/pitch in bytes
 */
void copy_video_plane_optimized(uint8_t *dst, const uint8_t *src,
                                uint32_t width, uint32_t height,
                                uint32_t dst_stride, uint32_t src_stride)
{
	if (!height || !width)
		return;

	if (width > src_stride || width > dst_stride) {
		blog(LOG_WARNING,
		     "copy_video_plane_optimized: width %u exceeds stride "
		     "(src_stride=%u dst_stride=%u) — copy skipped",
		     width, src_stride, dst_stride);
		return;
	}

	/* Detect overlap and fall back to memmove (line-by-line). The SIMD
	 * paths below assume non-overlapping buffers. We compare via uintptr_t
	 * casts rather than raw pointer relational ops, which is well-defined
	 * across unrelated allocations on flat address spaces (x86-64, ARM64).
	 * Check the full-extent byte ranges so legitimate back-to-back
	 * single-line buffers don't trip the fallback. */
	{
		const uint8_t *src_end = src + (size_t)(height - 1) * src_stride + width;
		const uint8_t *dst_end = dst + (size_t)(height - 1) * dst_stride + width;
		const bool overlap = !((uintptr_t)dst >= (uintptr_t)src_end ||
				       (uintptr_t)src >= (uintptr_t)dst_end);
		if (overlap) {
			for (uint32_t y = 0; y < height; y++) {
				const uint8_t *src_line = src + (size_t)y * src_stride;
				uint8_t *dst_line = dst + (size_t)y * dst_stride;
				memmove(dst_line, src_line, width);
			}
			return;
		}
	}
	if ((size_t)src_stride > SIZE_MAX / height ||
	    (size_t)dst_stride > SIZE_MAX / height) {
		blog(LOG_ERROR,
		     "copy_video_plane_optimized: stride*height would overflow "
		     "(h=%u dst_stride=%u src_stride=%u) — copy skipped",
		     height, dst_stride, src_stride);
		return;
	}

#if !OBS_X86_SIMD
	for (uint32_t y = 0; y < height; y++) {
		const uint8_t *src_line = src + (size_t)y * src_stride;
		uint8_t *dst_line = dst + (size_t)y * dst_stride;
		memcpy(dst_line, src_line, width);
	}
	return;
#else
	/* _mm_stream_si128 requires 16-byte aligned destination.
	 * Check once on the base pointer; if stride is also a multiple of 16
	 * then every subsequent line will also be aligned. */
	const bool dst_is_aligned = (((uintptr_t)dst) % 16 == 0);
	const bool stride_is_aligned = (dst_stride % 16 == 0);
	const bool can_use_nt = dst_is_aligned && stride_is_aligned;

	/* Only use non-temporal stores for large planes (>256 KB) where the
	 * cache-bypass benefit outweighs the overhead. */
	const bool use_nt_stores = can_use_nt && ((size_t)width * height) > (256 * 1024);

	/* ------------------------------------------------------------------ */
	/* Fast path: contiguous layout (same stride) — single bulk operation  */
	if (width == dst_stride && width == src_stride) {
		const size_t total_size = (size_t)width * (size_t)height;

		if (use_nt_stores && total_size >= 64) {
			const size_t simd_count = total_size & ~(size_t)63;
			size_t i;

			for (i = 0; i < simd_count; i += 64) {
				_mm_prefetch((const char *)(src + i + 128), _MM_HINT_NTA);

				__m128i d0 = _mm_loadu_si128((const __m128i *)(src + i +  0));
				__m128i d1 = _mm_loadu_si128((const __m128i *)(src + i + 16));
				__m128i d2 = _mm_loadu_si128((const __m128i *)(src + i + 32));
				__m128i d3 = _mm_loadu_si128((const __m128i *)(src + i + 48));

				/* dst + i is 16-byte aligned here (checked above) */
				_mm_stream_si128((__m128i *)(dst + i +  0), d0);
				_mm_stream_si128((__m128i *)(dst + i + 16), d1);
				_mm_stream_si128((__m128i *)(dst + i + 32), d2);
				_mm_stream_si128((__m128i *)(dst + i + 48), d3);
			}

			if (i < total_size)
				memcpy(dst + i, src + i, total_size - i);

			_mm_sfence();
		} else {
			memcpy(dst, src, total_size);
		}
		return;
	}

	/* ------------------------------------------------------------------ */
	/* Strided path: copy line-by-line                                     */
	bool need_sfence = false;

	for (uint32_t y = 0; y < height; y++) {
		const uint8_t *src_line = src + (size_t)y * src_stride;
		uint8_t *dst_line       = dst + (size_t)y * dst_stride;

		/* Prefetch the next source line into L1 */
		if (y + 1 < height)
			_mm_prefetch((const char *)(src + (size_t)(y + 1) * src_stride), _MM_HINT_T0);

		if (use_nt_stores && width >= 64) {
			const size_t simd_count = width & ~(size_t)63;
			size_t i;

			for (i = 0; i < simd_count; i += 64) {
				__m128i d0 = _mm_loadu_si128((const __m128i *)(src_line + i +  0));
				__m128i d1 = _mm_loadu_si128((const __m128i *)(src_line + i + 16));
				__m128i d2 = _mm_loadu_si128((const __m128i *)(src_line + i + 32));
				__m128i d3 = _mm_loadu_si128((const __m128i *)(src_line + i + 48));

				/* dst_line + i is 16-byte aligned (checked above) */
				_mm_stream_si128((__m128i *)(dst_line + i +  0), d0);
				_mm_stream_si128((__m128i *)(dst_line + i + 16), d1);
				_mm_stream_si128((__m128i *)(dst_line + i + 32), d2);
				_mm_stream_si128((__m128i *)(dst_line + i + 48), d3);
			}

			if (i < width)
				memcpy(dst_line + i, src_line + i, width - i);

			need_sfence = true;
		} else if (can_use_nt && width >= 16) {
			/* Destination aligned but plane too small for NT stores:
			 * use unaligned vectorised stores (safe, no alignment req) */
			const size_t simd_count = width & ~(size_t)15;
			size_t i;

			for (i = 0; i < simd_count; i += 16) {
				__m128i d0 = _mm_loadu_si128((const __m128i *)(src_line + i));
				_mm_storeu_si128((__m128i *)(dst_line + i), d0);
			}
			if (i < width)
				memcpy(dst_line + i, src_line + i, width - i);
		} else {
			memcpy(dst_line, src_line, width);
		}
	}

	if (need_sfence)
		_mm_sfence();
#endif
}

/**
 * Fast zero-fill for audio buffers
 * Uses SIMD instructions for better performance than memset
 */
void zero_audio_buffer_optimized(float *buffer, size_t count)
{
	size_t i = 0;

#if OBS_X86_SIMD
	if (cpu_supports_avx2() && count >= 8) {
		zero_audio_buffer_avx2(buffer, count);
		return;
	}
	{
		__m128 zero = _mm_setzero_ps();
		const size_t sse_count = count & ~(size_t)3;
		for (i = 0; i < sse_count; i += 4) {
			_mm_storeu_ps(&buffer[i], zero);
		}
	}
#endif

	// Handle remaining elements
	for (; i < count; i++) {
		buffer[i] = 0.0f;
	}
}
