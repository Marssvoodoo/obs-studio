/******************************************************************************
 * OBS Studio - deterministic audio worker sizing primitive
 ******************************************************************************/

#pragma once

#include <stddef.h>

/* Hard ceiling on worker count regardless of core count. Audio rendering
 * rarely benefits past this point because source jobs are short and wake-up
 * overhead begins to dominate. */
#define OBS_AUDIO_MAX_THREADS 16u

/* Approximate target sources per worker. This avoids waking a large pool for
 * a scene that only has a few active audio sources. */
#define OBS_AUDIO_DEFAULT_WORKER_BUDGET 4u

static inline size_t obs_audio_threadpool_recommended_threads_impl(size_t logical_cores, size_t expected_jobs)
{
	if (logical_cores <= 1)
		return 0;

	size_t workers = logical_cores - 1;
	if (workers > OBS_AUDIO_MAX_THREADS)
		workers = OBS_AUDIO_MAX_THREADS;

	if (expected_jobs > 0) {
		const size_t job_workers = 1 + (expected_jobs - 1) / OBS_AUDIO_DEFAULT_WORKER_BUDGET;
		if (workers > job_workers)
			workers = job_workers;
	}

	return workers;
}
