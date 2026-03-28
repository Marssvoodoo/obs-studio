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
		_mm_prefetch((const char *)(aud + i + 16), _MM_HINT_T0);
		_mm_prefetch((const char *)(mix + i + 16), _MM_HINT_T0);

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
/*
 * AVX2 audio mixing — processes 8 floats per iteration.
 *
 * On MSVC, AVX2 intrinsics are available without /arch:AVX2 (the compiler
 * emits the VEX-encoded instruction inline).  On GCC/Clang we need an
 * explicit target attribute so the compiler allows the intrinsics.
 */
#if !defined(_MSC_VER)
__attribute__((target("avx2")))
#endif
static inline void mix_audio_avx2(float *mix, const float *aud, size_t count)
{
	size_t i = 0;
	const size_t simd_count = count & ~(size_t)7;

	for (i = 0; i < simd_count; i += 8) {
		_mm_prefetch((const char *)(aud + i + 16), _MM_HINT_T0);
		_mm_prefetch((const char *)(mix + i + 16), _MM_HINT_T0);

		__m256 v_mix = _mm256_loadu_ps(&mix[i]);
		__m256 v_aud = _mm256_loadu_ps(&aud[i]);
		__m256 v_result = _mm256_add_ps(v_mix, v_aud);
		_mm256_storeu_ps(&mix[i], v_result);
	}

	_mm256_zeroupper(); /* Avoid AVX-SSE transition penalty */

	for (; i < count; i++) {
		mix[i] += aud[i];
	}
}
#endif

/**
 * Optimized multi-channel audio mixing
 * This is the drop-in replacement for the hot loop in mix_audio()
 *
 * Uses SIMD instructions when available for 4-8x performance improvement
 *
 * @param mixes         Output mix buffers
 * @param channels      Number of active audio channels
 * @param audio_buffers Source per-mix per-channel float buffers (float *[][MAX_AUDIO_CHANNELS])
 * @param start_point   First sample offset within the output mix buffer
 * @param total_floats  Number of samples to mix
 */
void mix_audio_optimized(struct audio_output_data *mixes, size_t channels,
                         float *(*audio_buffers)[MAX_AUDIO_CHANNELS],
                         size_t start_point, size_t total_floats)
{
	for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
		for (size_t ch = 0; ch < channels; ch++) {
			float *mix = mixes[mix_idx].data[ch] + start_point;
			float *aud = audio_buffers[mix_idx][ch];

			// Choose best SIMD path based on CPU capabilities
#if OBS_X86_SIMD
			if (cpu_supports_avx2() && total_floats >= 8) {
				mix_audio_avx2(mix, aud, total_floats);
			} else
#endif
#if OBS_X86_SIMD
			if (total_floats >= 4) {
				mix_audio_sse2(mix, aud, total_floats);
			} else {
				// Fallback for very small buffers
				for (size_t i = 0; i < total_floats; i++) {
					mix[i] += aud[i];
				}
			}
#else
			for (size_t i = 0; i < total_floats; i++) {
				mix[i] += aud[i];
			}
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
	if (width > src_stride || width > dst_stride)
		return;
	if ((size_t)src_stride > SIZE_MAX / height ||
	    (size_t)dst_stride > SIZE_MAX / height)
		return; /* stride * height would overflow */

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
	const size_t simd_count = count & ~7; // Round down to multiple of 8
	
#if OBS_X86_SIMD
	if (cpu_supports_avx2() && count >= 8) {
		__m256 zero = _mm256_setzero_ps();
		for (i = 0; i < simd_count; i += 8) {
			_mm256_storeu_ps(&buffer[i], zero);
		}
		_mm256_zeroupper();
	} else
#endif
	{
#if OBS_X86_SIMD
		__m128 zero = _mm_setzero_ps();
		const size_t sse_count = count & ~3;
		for (i = 0; i < sse_count; i += 4) {
			_mm_storeu_ps(&buffer[i], zero);
		}
#endif
	}
	
	// Handle remaining elements
	for (; i < count; i++) {
		buffer[i] = 0.0f;
	}
}
