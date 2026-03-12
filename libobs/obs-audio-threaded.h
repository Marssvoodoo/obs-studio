/******************************************************************************
 * OBS Studio — Multi-threaded Audio Pipeline (Phase 6.4)
 *
 * A fixed-size worker pool that processes one batch of audio jobs per audio
 * callback, replacing the sequential per-source render loop in obs-audio.c.
 *
 * Architecture:
 *   - One coordinator thread (the OBS audio thread) publishes a batch of
 *     jobs and bumps a generation counter.
 *   - N worker threads wake for the new generation and cooperatively drain
 *     the batch via an atomic next-job index.
 *   - The coordinator thread also participates in draining the batch, then
 *     waits for the outstanding-job counter to reach zero.
 *   - Audio mixing is performed serially AFTER all parallel renders complete,
 *     because mix_audio() accumulates into shared output buffers.
 *
 * This avoids the per-job mutex/condvar overhead of the earlier queue-based
 * implementation and matches the single-producer, bursty nature of the audio
 * callback much more closely.
 ******************************************************************************/

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque pool handle ──────────────────────────────────────────────────── */
struct obs_audio_threadpool;

/* ── Job descriptor ──────────────────────────────────────────────────────── */
typedef void (*audio_job_fn)(void *param);

struct audio_job {
	audio_job_fn fn;    /* function to call                              */
	void        *param; /* opaque parameter forwarded to fn()            */
};

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/*
 * obs_audio_threadpool_create
 *
 * @num_threads   Number of worker threads to spawn.  Pass 0 to auto-detect
 *               (= logical-core-count - 1, clamped to [1, 16], disabled on
 *               single-core systems).
 * @queue_cap     Unused legacy parameter. Pass 0.
 *
 * Returns NULL on failure.
 */
struct obs_audio_threadpool *obs_audio_threadpool_create(size_t num_threads,
							 size_t queue_cap);

/*
 * obs_audio_threadpool_destroy
 *
 * Signals all workers to stop, waits for the current batch to drain, then
 * joins and frees everything. Safe to call with NULL.
 */
void obs_audio_threadpool_destroy(struct obs_audio_threadpool *pool);

/* ── Job submission ──────────────────────────────────────────────────────── */

/*
 * obs_audio_threadpool_run
 *
 * Publish one batch of jobs and block until the batch has completed.
 * Must be called from the audio coordinator only.
 */
void obs_audio_threadpool_run(struct obs_audio_threadpool *pool,
			      const struct audio_job *jobs, size_t num_jobs);

/* ── Diagnostics ─────────────────────────────────────────────────────────── */

/* Return the number of worker threads. */
size_t obs_audio_threadpool_num_threads(const struct obs_audio_threadpool *pool);

/* Return peak batch size observed since last reset. */
size_t obs_audio_threadpool_peak_batch_size(const struct obs_audio_threadpool *pool);

/* Reset peak-batch counter. */
void obs_audio_threadpool_reset_stats(struct obs_audio_threadpool *pool);

#ifdef __cplusplus
}
#endif
