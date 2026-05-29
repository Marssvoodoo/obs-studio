/******************************************************************************
 * OBS Studio — Multi-threaded Audio Pipeline implementation (Phase 6.4)
 *
 * See obs-audio-threaded.h for architecture overview.
 *
 * NOTE: Uses OBS's os_atomic_* wrappers instead of C11 <stdatomic.h> to
 *       remain compatible with MSVC in C99/C11 mode.
 ******************************************************************************/

#include "obs-audio-threaded.h"
#include "util/threading.h"
#include <limits.h>
#include "util/bmem.h"
#include "util/base.h"
#include "util/platform.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>  /* _mm_pause for CAS-loop spin hint */
#define OBS_AUDIO_CPU_PAUSE() _mm_pause()
#else
#define OBS_AUDIO_CPU_PAUSE() ((void)0)
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */
/* Hard ceiling on worker count regardless of core count. The audio render
 * graph rarely benefits past ~8 workers because per-source work is small
 * (~10–50 µs at 44.1/48 kHz @ 480 frames) and cond/mutex wake-up cost
 * (~3–5 µs per worker) dominates. Past 16 workers the wake-up overhead
 * starts to eat the parallelism gains. Tuned empirically; if you raise
 * this, also raise OBS_AUDIO_DEFAULT_WORKER_BUDGET so the per-source
 * count target keeps pace. */
#ifndef OBS_AUDIO_MAX_THREADS
#define OBS_AUDIO_MAX_THREADS 16u
#endif
#define MAX_THREADS OBS_AUDIO_MAX_THREADS
/* Approximate target sources-per-worker. Used to clamp num_threads on
 * machines with many more cores than active sources, so an idle 64-core
 * Threadripper doesn't wake 16 workers 60×/sec for a 2-source scene. */
#define OBS_AUDIO_DEFAULT_WORKER_BUDGET 4u

/* ── Thread pool ─────────────────────────────────────────────────────────── */
struct obs_audio_threadpool {
	pthread_t *threads;
	size_t num_threads;

	pthread_mutex_t work_mutex;
	pthread_cond_t work_cond;
	pthread_cond_t done_cond;
	bool shutdown;

	const struct audio_job *jobs;
	volatile long num_jobs;

	volatile long next_job;
	volatile long pending;
	/* Count of worker threads that have woken for the current batch and are
	 * still inside drain_batch().  obs_audio_threadpool_run() waits for this
	 * to reach zero (in addition to pending) before returning, so no worker
	 * can outlive the batch and dereference the jobs[] array after the
	 * caller frees/reuses it on a later tick. Touched only under
	 * work_mutex / via os_atomic. */
	volatile long active_workers;
	volatile long batch_serial;
	volatile long peak_batch_size;
};

static bool claim_job(struct obs_audio_threadpool *pool, struct audio_job *job)
{
	/* Snapshot pool->jobs once via volatile read into a local. The
	 * coordinator clears pool->jobs to NULL after drain in
	 * obs_audio_threadpool_run; without a local snapshot a worker that
	 * passed the entry-check could observe NULL on the post-CAS
	 * dereference (`pool->jobs[idx]`) on weakly-ordered platforms (ARM64)
	 * and SEGV. We also re-read num_jobs each iteration in case of a
	 * rebatch, but the jobs[] backing array stays stable for the duration
	 * of one batch_serial. */
	struct audio_job *jobs =
		*(struct audio_job *volatile *)&pool->jobs;
	if (!jobs)
		return false;

	/* Bound the CAS-loop iterations so a misbehaving filter that never
	 * returns can't pin a worker forever during shutdown. The realistic
	 * worst case is `num_jobs - 1` losers per round; 4096 is well past
	 * any plausible scene-source-count × hyperthread sibling churn. */
	const int kMaxClaimSpin = 4096;
	for (int spin = 0; spin < kMaxClaimSpin; spin++) {
		if (os_atomic_load_bool(&pool->shutdown))
			return false;

		long num = os_atomic_load_long(&pool->num_jobs);
		long idx = os_atomic_load_long(&pool->next_job);
		if (idx >= num)
			return false;

		if (!os_atomic_compare_swap_long(&pool->next_job, idx, idx + 1)) {
			/* Hyperthread sibling lost the CAS race; yield the
			 * pipeline so the winner can make progress and we
			 * don't hammer the cache line. */
			OBS_AUDIO_CPU_PAUSE();
			continue;
		}

		*job = jobs[idx];
		return true;
	}
	return false;
}

static void finish_job(struct obs_audio_threadpool *pool)
{
	long remaining = os_atomic_dec_long(&pool->pending);
	if (remaining == 0) {
		pthread_mutex_lock(&pool->work_mutex);
		pthread_cond_broadcast(&pool->done_cond);
		pthread_mutex_unlock(&pool->work_mutex);
	}
}

static void drain_batch(struct obs_audio_threadpool *pool)
{
	struct audio_job job;

	while (claim_job(pool, &job)) {
		job.fn(job.param);
		finish_job(pool);
	}
}

/* Worker thread entry */
static void *worker_thread(void *arg)
{
	struct obs_audio_threadpool *pool = arg;
	long seen_serial = 0;
	os_set_thread_name("obs-audio-worker");

	while (true) {
		pthread_mutex_lock(&pool->work_mutex);
		while (!os_atomic_load_bool(&pool->shutdown) &&
		       os_atomic_load_long(&pool->batch_serial) == seen_serial)
			pthread_cond_wait(&pool->work_cond, &pool->work_mutex);
		if (os_atomic_load_bool(&pool->shutdown)) {
			pthread_mutex_unlock(&pool->work_mutex);
			break;
		}

		seen_serial = os_atomic_load_long(&pool->batch_serial);
		/* Register as active for this batch BEFORE releasing the mutex.
		 * obs_audio_threadpool_run() reads active_workers under the same
		 * mutex, so it can never observe us as idle while we go on to
		 * dereference the jobs[] array — closing the window where a
		 * straggling worker outlived the batch and read a freed/reused
		 * jobs array on a later tick. */
		os_atomic_inc_long(&pool->active_workers);
		pthread_mutex_unlock(&pool->work_mutex);

		drain_batch(pool);

		/* Leaving the batch: drop our active count and, once the batch is
		 * fully drained (no pending jobs and no other active worker),
		 * wake the coordinator blocked in obs_audio_threadpool_run() /
		 * obs_audio_threadpool_destroy(). */
		pthread_mutex_lock(&pool->work_mutex);
		if (os_atomic_dec_long(&pool->active_workers) == 0 &&
		    os_atomic_load_long(&pool->pending) == 0)
			pthread_cond_broadcast(&pool->done_cond);
		pthread_mutex_unlock(&pool->work_mutex);
	}

	return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

struct obs_audio_threadpool *obs_audio_threadpool_create(size_t num_threads,
							 size_t queue_cap)
{
	UNUSED_PARAMETER(queue_cap);

	if (num_threads == 0) {
		size_t cores = (size_t)os_get_logical_cores();
		if (cores <= 1)
			return NULL;
		num_threads = cores - 1;
		if (num_threads > MAX_THREADS)
			num_threads = MAX_THREADS;
	}

	struct obs_audio_threadpool *pool =
		bzalloc(sizeof(struct obs_audio_threadpool));
	if (!pool)
		return NULL;

	pool->threads = bmalloc(num_threads * sizeof(pthread_t));
	if (!pool->threads) {
		bfree(pool);
		return NULL;
	}

	pool->num_threads = num_threads;
	os_atomic_set_bool(&pool->shutdown, false);
	pool->jobs = NULL;
	pool->num_jobs = 0;
	pool->next_job = 0;
	pool->pending = 0;
	pool->active_workers = 0;
	pool->batch_serial = 0;
	pool->peak_batch_size = 0;

	pthread_mutex_init(&pool->work_mutex, NULL);
	pthread_cond_init(&pool->work_cond, NULL);
	pthread_cond_init(&pool->done_cond, NULL);

	for (size_t i = 0; i < num_threads; i++) {
		if (pthread_create(&pool->threads[i], NULL, worker_thread,
				   pool) != 0) {
			pthread_mutex_lock(&pool->work_mutex);
			os_atomic_set_bool(&pool->shutdown, true);
			pthread_cond_broadcast(&pool->work_cond);
			pthread_mutex_unlock(&pool->work_mutex);
			for (size_t j = 0; j < i; j++)
				pthread_join(pool->threads[j], NULL);
			pthread_mutex_destroy(&pool->work_mutex);
			pthread_cond_destroy(&pool->work_cond);
			pthread_cond_destroy(&pool->done_cond);
			bfree(pool->threads);
			bfree(pool);
			return NULL;
		}
	}

	blog(LOG_INFO,
	     "obs-audio-threaded: pool created — %zu worker thread(s)",
	     num_threads);

	return pool;
}

void obs_audio_threadpool_destroy(struct obs_audio_threadpool *pool)
{
	if (!pool)
		return;

	pthread_mutex_lock(&pool->work_mutex);
	while (os_atomic_load_long(&pool->pending) > 0 ||
	       os_atomic_load_long(&pool->active_workers) > 0)
		pthread_cond_wait(&pool->done_cond, &pool->work_mutex);
	os_atomic_set_bool(&pool->shutdown, true);
	pthread_cond_broadcast(&pool->work_cond);
	pthread_mutex_unlock(&pool->work_mutex);

	for (size_t i = 0; i < pool->num_threads; i++)
		pthread_join(pool->threads[i], NULL);

	pthread_mutex_destroy(&pool->work_mutex);
	pthread_cond_destroy(&pool->work_cond);
	pthread_cond_destroy(&pool->done_cond);
	bfree(pool->threads);
	bfree(pool);
}

void obs_audio_threadpool_run(struct obs_audio_threadpool *pool,
			      const struct audio_job *jobs, size_t num_jobs)
{
	if (!jobs || !num_jobs)
		return;

	if (!pool) {
		for (size_t i = 0; i < num_jobs; i++)
			jobs[i].fn(jobs[i].param);
		return;
	}

	/* Clamp to LONG_MAX to prevent truncation on LLP64 (Windows). Hitting
	 * this means a caller queued >2 billion jobs in one batch — programmer
	 * bug, not a runtime condition we expect. Log loud so it surfaces. */
	if (num_jobs > (size_t)LONG_MAX) {
		blog(LOG_ERROR,
		     "obs_audio_threadpool_run: batch of %zu jobs clamped to "
		     "LONG_MAX — caller bug, jobs past the clamp will silently "
		     "be skipped",
		     num_jobs);
		num_jobs = (size_t)LONG_MAX;
	}
	long batch_size = (long)num_jobs;
	long cur_peak;
	do {
		cur_peak = os_atomic_load_long(&pool->peak_batch_size);
		if (batch_size <= cur_peak)
			break;
	} while (!os_atomic_compare_exchange_long(&pool->peak_batch_size,
						  &cur_peak, batch_size));

	pthread_mutex_lock(&pool->work_mutex);
	pool->jobs = jobs;
	os_atomic_set_long(&pool->num_jobs, (long)num_jobs);
	os_atomic_set_long(&pool->next_job, 0);
	os_atomic_set_long(&pool->pending, batch_size);
	os_atomic_inc_long(&pool->batch_serial);
	pthread_cond_broadcast(&pool->work_cond);
	pthread_mutex_unlock(&pool->work_mutex);

	/* Let the coordinator drain work too so small batches do not pay a
	 * full worker wake-up penalty. */
	drain_batch(pool);

	pthread_mutex_lock(&pool->work_mutex);
	while (os_atomic_load_long(&pool->pending) > 0 ||
	       os_atomic_load_long(&pool->active_workers) > 0)
		pthread_cond_wait(&pool->done_cond, &pool->work_mutex);
	pool->jobs = NULL;
	os_atomic_set_long(&pool->num_jobs, 0);
	pthread_mutex_unlock(&pool->work_mutex);
}

size_t obs_audio_threadpool_num_threads(const struct obs_audio_threadpool *pool)
{
	return pool ? pool->num_threads : 0;
}

size_t obs_audio_threadpool_peak_batch_size(const struct obs_audio_threadpool *pool)
{
	/* peak_batch_size is volatile long — safe to read directly. */
	return pool ? (size_t)pool->peak_batch_size : 0;
}

void obs_audio_threadpool_reset_stats(struct obs_audio_threadpool *pool)
{
	if (pool)
		os_atomic_set_long(&pool->peak_batch_size, 0);
}
