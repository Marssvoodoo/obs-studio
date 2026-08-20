#include <obs-audio-worker-sizing.h>
#include <obs-video-overlap.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE 96

static int check_overlap_copy(size_t dst_offset, size_t src_offset, uint32_t width, uint32_t height,
			      uint32_t dst_stride, uint32_t src_stride)
{
	uint8_t actual[BUFFER_SIZE];
	uint8_t expected[BUFFER_SIZE];
	uint8_t overlap_buffer[BUFFER_SIZE];
	uint8_t snapshot[BUFFER_SIZE];

	for (size_t index = 0; index < BUFFER_SIZE; ++index)
		actual[index] = (uint8_t)(index * 17U + 3U);
	memcpy(expected, actual, sizeof(expected));
	memcpy(snapshot, actual, sizeof(snapshot));

	for (uint32_t row = 0; row < height; ++row) {
		memcpy(expected + dst_offset + (size_t)row * dst_stride,
		       snapshot + src_offset + (size_t)row * src_stride, width);
	}

	obs_copy_overlapping_video_plane(actual + dst_offset, actual + src_offset, width, height, dst_stride,
					 src_stride, overlap_buffer);
	if (memcmp(actual, expected, sizeof(actual)) != 0) {
		fprintf(stderr, "overlap copy failed: dst=%zu src=%zu width=%u height=%u\n", dst_offset, src_offset,
			width, height);
		return 1;
	}

	return 0;
}

static int check_worker_count(size_t cores, size_t jobs, size_t expected)
{
	const size_t actual = obs_audio_threadpool_recommended_threads_impl(cores, jobs);
	if (actual != expected) {
		fprintf(stderr, "worker count failed: cores=%zu jobs=%zu expected=%zu actual=%zu\n", cores, jobs,
			expected, actual);
		return 1;
	}
	return 0;
}

int main(void)
{
	int failures = 0;

	/* Both directions used to corrupt later rows when overlap was handled
	 * with independent row-level memmove calls. */
	failures += check_overlap_copy(3, 0, 6, 4, 6, 6);
	failures += check_overlap_copy(0, 3, 6, 4, 6, 6);
	failures += check_overlap_copy(4, 0, 5, 4, 7, 8);

	failures += check_worker_count(0, 100, 0);
	failures += check_worker_count(1, 100, 0);
	failures += check_worker_count(16, 3, 1);
	failures += check_worker_count(16, 5, 2);
	failures += check_worker_count(64, 100, 16);
	failures += check_worker_count(8, 0, 7);

	return failures ? 1 : 0;
}
