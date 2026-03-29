/******************************************************************************
    OBS Studio - Audio Pipeline Optimizations (AVX2)

    This file contains AVX2-specific SIMD functions that require /arch:AVX2
    (MSVC) or -mavx2 (GCC/Clang) to compile. Functions here are called via
    runtime dispatch after confirming AVX2 support.
******************************************************************************/

#include <stdint.h>
#include <string.h>

#include "obs-internal.h"
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
/*
 * AVX2 audio mixing — processes 8 floats per iteration.
 *
 * On MSVC, this file is compiled with /arch:AVX2.  On GCC/Clang we use
 * -mavx2 at the file level and also keep the target attribute for clarity.
 */
#if !defined(_MSC_VER)
__attribute__((target("avx2")))
#endif
void mix_audio_avx2(float *mix, const float *aud, size_t count)
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

	/* Issue zeroupper BEFORE the scalar tail loop: transitioning from
	 * AVX (256-bit) to SSE/scalar without clearing the upper YMM halves
	 * causes a costly (~70-cycle) state-transition penalty on Intel CPUs. */
	_mm256_zeroupper();

	for (; i < count; i++) {
		mix[i] += aud[i];
	}
}

/*
 * AVX2 zero-fill for audio buffers — processes 8 floats per iteration.
 */
#if !defined(_MSC_VER)
__attribute__((target("avx2")))
#endif
void zero_audio_buffer_avx2(float *buffer, size_t count)
{
	size_t i = 0;
	const size_t simd_count = count & ~(size_t)7;
	__m256 zero = _mm256_setzero_ps();

	for (i = 0; i < simd_count; i += 8) {
		_mm256_storeu_ps(&buffer[i], zero);
	}
	/* Zeroupper before scalar tail — see mix_audio_avx2 comment. */
	_mm256_zeroupper();

	for (; i < count; i++) {
		buffer[i] = 0.0f;
	}
}
#endif
