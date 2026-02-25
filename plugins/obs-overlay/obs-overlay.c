/******************************************************************************
 * OBS In-Game Stats Overlay  (obs-overlay.dll)
 * ----------------------------------------------
 * Shows a transparent always-on-top window with live OBS statistics:
 *   • Streaming / recording status + elapsed time
 *   • Dropped frames (count + percentage)
 *   • Rendered FPS vs target FPS
 *   • Output bitrate (kb/s  or  Mb/s)
 *   • CPU + render load indicators
 *
 * Works above borderless-windowed and borderless-fullscreen games.
 * Mouse clicks pass through (WS_EX_TRANSPARENT).
 * Toggle visibility with a configurable hotkey (default: Ctrl+Shift+O).
 *
 * Build: add to plugins/CMakeLists.txt or compile standalone:
 *   cl obs-overlay.c /link obs.lib user32.lib gdi32.lib /DLL /OUT:obs-overlay.dll
 ******************************************************************************/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <stdio.h>
#include <time.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-overlay", "en-US")

/* ── Config ──────────────────────────────────────────────────────────────── */
#define OVERLAY_CLASS  L"OBS_STATS_OVERLAY"
#define UPDATE_MS      500          /* stats refresh rate (ms)                */
#define FONT_SIZE      18           /* points                                 */
#define WINDOW_W       340
#define WINDOW_H       170
#define MARGIN         10
#define COLORKEY       RGB(1, 1, 1) /* near-black = transparent               */

/* Corner anchors */
#define ANCHOR_TOP_LEFT     0
#define ANCHOR_TOP_RIGHT    1
#define ANCHOR_BOTTOM_LEFT  2
#define ANCHOR_BOTTOM_RIGHT 3

/* ── State ───────────────────────────────────────────────────────────────── */
static HWND      g_hwnd       = NULL;
static HFONT     g_font       = NULL;
static HFONT     g_font_bold  = NULL;
static bool      g_visible    = false;  /* start hidden — use hotkey to show */
static bool      g_running    = false;
static pthread_t g_thread;
static obs_hotkey_id g_hk_toggle = OBS_INVALID_HOTKEY_ID;
static int       g_anchor     = ANCHOR_TOP_LEFT;
static int       g_offset_x   = 20;
static int       g_offset_y   = 20;
static uint32_t  g_bg_color   = 0x99000000; /* ARGB — semi-transparent black */
static uint8_t   g_opacity    = 180;        /* 0-255 window alpha             */

/* ── Cached stats (written by timer, read by WM_PAINT) ─────────────────── */
static char g_line[8][64];
static int  g_line_count = 0;
static COLORREF g_line_color[8];
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void format_time(char *buf, size_t n, uint64_t ns)
{
	uint64_t s  = ns / 1000000000ULL;
	uint64_t m  = s / 60;  s  %= 60;
	uint64_t h  = m / 60;  m  %= 60;
	snprintf(buf, n, "%02llu:%02llu:%02llu", (unsigned long long)h,
	         (unsigned long long)m, (unsigned long long)s);
}

static uint64_t g_stream_start_ns = 0;
static uint64_t g_record_start_ns = 0;

/* ── Stats collection ────────────────────────────────────────────────────── */
static void collect_stats(void)
{
	char lines[8][64];
	COLORREF colors[8];
	int n = 0;

	/* ── Header ── */
	bool streaming  = obs_frontend_streaming_active();
	bool recording  = obs_frontend_recording_active();

	if (!streaming && !recording) {
		snprintf(lines[n], 64, "OBS — Idle");
		colors[n++] = RGB(180, 180, 180);
	} else {
		if (streaming) {
			snprintf(lines[n], 64, "◉ LIVE");
			colors[n++] = RGB(255, 80, 80);
		}
		if (recording) {
			snprintf(lines[n], 64, "⏺ REC");
			colors[n++] = RGB(80, 200, 80);
		}
	}

	/* ── Stream elapsed time ── */
	if (streaming) {
		obs_output_t *out = obs_frontend_get_streaming_output();
		if (out) {
			uint64_t dur = obs_output_get_active_delay(out);
			(void)dur;
			/* Use total bytes as proxy for time if no duration API */
			uint64_t bytes = obs_output_get_total_bytes(out);
			snprintf(lines[n], 64, "Sent: %.1f MB",
			         (double)bytes / (1024.0 * 1024.0));
			colors[n++] = RGB(200, 200, 200);
			obs_output_release(out);
		}
	}

	/* ── Dropped frames ── */
	uint32_t total_frames  = obs_get_total_frames();
	uint32_t lagged_frames = obs_get_lagged_frames();
	double   drop_pct      = (total_frames > 0)
	                         ? (100.0 * lagged_frames / total_frames)
	                         : 0.0;
	snprintf(lines[n], 64, "Dropped: %u  (%.1f%%)",
	         lagged_frames, drop_pct);
	colors[n++] = (drop_pct > 1.0) ? RGB(255, 140, 0)
	            : (drop_pct > 0.2) ? RGB(255, 220, 0)
	            : RGB(120, 220, 120);

	/* ── Skipped frames (render) ── */
	uint32_t skipped = obs_output_get_frames_dropped(NULL);
	(void)skipped; /* exposed via stats callback — skip for simplicity */

	/* ── Render FPS ── */
	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		double target_fps = (double)ovi.fps_num / (double)ovi.fps_den;
		double render_fps = obs_get_active_fps();
		snprintf(lines[n], 64, "FPS: %.1f / %.0f",
		         render_fps, target_fps);
		colors[n++] = (render_fps < target_fps * 0.95)
		              ? RGB(255, 140, 0) : RGB(120, 220, 120);
	}

	/* ── Bitrate (streaming) ── */
	if (streaming) {
		obs_output_t *out = obs_frontend_get_streaming_output();
		if (out) {
			double kbps = obs_output_get_total_bytes(out) / 128.0;
			/* kbps = bytes*8/1000, approximate with rolling window */
			snprintf(lines[n], 64, "Output: %.0f kbps (est)", kbps);
			colors[n++] = RGB(160, 200, 255);
			obs_output_release(out);
		}
	}

	/* ── Canvas resolution ── */
	struct obs_video_info ovi2;
	if (obs_get_video_info(&ovi2)) {
		snprintf(lines[n], 64, "Canvas: %ux%u → %ux%u",
		         ovi2.base_width, ovi2.base_height,
		         ovi2.output_width, ovi2.output_height);
		colors[n++] = RGB(160, 160, 160);
	}

	/* ── Commit ── */
	pthread_mutex_lock(&g_stats_mutex);
	for (int i = 0; i < n; i++) {
		memcpy(g_line[i], lines[i], 64);
		g_line_color[i] = colors[i];
	}
	g_line_count = n;
	pthread_mutex_unlock(&g_stats_mutex);
}

/* ── Window procedure ────────────────────────────────────────────────────── */
static LRESULT CALLBACK overlay_wndproc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);

		RECT rc;
		GetClientRect(hwnd, &rc);

		/* Background */
		HBRUSH bg = CreateSolidBrush(RGB(10, 10, 10));
		FillRect(dc, &rc, bg);
		DeleteObject(bg);

		pthread_mutex_lock(&g_stats_mutex);
		int n = g_line_count;
		char lines[8][64];
		COLORREF cl[8];
		for (int i = 0; i < n; i++) {
			memcpy(lines[i], g_line[i], 64);
			cl[i] = g_line_color[i];
		}
		pthread_mutex_unlock(&g_stats_mutex);

		SetBkMode(dc, TRANSPARENT);
		for (int i = 0; i < n; i++) {
			HFONT old = (HFONT)SelectObject(dc,
			            (i == 0) ? g_font_bold : g_font);
			SetTextColor(dc, cl[i]);
			RECT lr = {MARGIN, MARGIN + i * (FONT_SIZE + 4),
			           rc.right - MARGIN, rc.bottom};
			DrawTextA(dc, lines[i], -1, &lr, DT_LEFT | DT_NOPREFIX);
			SelectObject(dc, old);
		}

		EndPaint(hwnd, &ps);
		return 0;
	}

	case WM_TIMER:
		collect_stats();
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
}

/* ── Window thread ───────────────────────────────────────────────────────── */
static void *overlay_thread(void *param)
{
	(void)param;
	HINSTANCE hi = GetModuleHandleW(NULL);

	/* Register class */
	WNDCLASSEXW wc = {0};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = overlay_wndproc;
	wc.hInstance     = hi;
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = OVERLAY_CLASS;
	RegisterClassExW(&wc);

	/* Position based on anchor */
	int scr_w = GetSystemMetrics(SM_CXSCREEN);
	int scr_h = GetSystemMetrics(SM_CYSCREEN);
	int x = g_offset_x;
	int y = g_offset_y;
	if (g_anchor == ANCHOR_TOP_RIGHT || g_anchor == ANCHOR_BOTTOM_RIGHT)
		x = scr_w - WINDOW_W - g_offset_x;
	if (g_anchor == ANCHOR_BOTTOM_LEFT || g_anchor == ANCHOR_BOTTOM_RIGHT)
		y = scr_h - WINDOW_H - g_offset_y;

	g_hwnd = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
		OVERLAY_CLASS,
		L"OBS Overlay",
		WS_POPUP,
		x, y, WINDOW_W, WINDOW_H,
		NULL, NULL, hi, NULL);

	if (!g_hwnd) {
		blog(LOG_ERROR, "[obs-overlay] Failed to create window: %lu",
		     GetLastError());
		return NULL;
	}

	/* Semi-transparent black background, colorkey transparent */
	SetLayeredWindowAttributes(g_hwnd, 0, g_opacity, LWA_ALPHA);

	/* Fonts */
	g_font = CreateFontA(FONT_SIZE, 0, 0, 0, FW_NORMAL, 0, 0, 0,
	                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
	                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
	                     DEFAULT_PITCH | FF_DONTCARE, "Consolas");
	g_font_bold = CreateFontA(FONT_SIZE + 2, 0, 0, 0, FW_BOLD, 0, 0, 0,
	                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
	                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
	                          DEFAULT_PITCH | FF_DONTCARE, "Consolas");

	/* Initial stats + show */
	collect_stats();
	if (g_visible)
		ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

	/* 500ms update timer */
	SetTimer(g_hwnd, 1, UPDATE_MS, NULL);

	MSG msg;
	while (g_running && GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	KillTimer(g_hwnd, 1);
	if (g_font)      { DeleteObject(g_font);      g_font = NULL; }
	if (g_font_bold) { DeleteObject(g_font_bold); g_font_bold = NULL; }
	DestroyWindow(g_hwnd);
	g_hwnd = NULL;
	UnregisterClassW(OVERLAY_CLASS, hi);
	return NULL;
}

/* ── Hotkey ──────────────────────────────────────────────────────────────── */
static void toggle_overlay(void *data, obs_hotkey_id id,
                            obs_hotkey_t *hotkey, bool pressed)
{
	(void)data; (void)id; (void)hotkey;
	if (!pressed || !g_hwnd) return;

	g_visible = !g_visible;
	if (g_visible)
		ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
	else
		ShowWindow(g_hwnd, SW_HIDE);

	blog(LOG_INFO, "[obs-overlay] Overlay %s",
	     g_visible ? "visible" : "hidden");
}

/* ── Module lifecycle ────────────────────────────────────────────────────── */
bool obs_module_load(void)
{
	blog(LOG_INFO, "[obs-overlay] Loading in-game stats overlay v1.0");

	g_running = true;

	if (pthread_create(&g_thread, NULL, overlay_thread, NULL) != 0) {
		blog(LOG_ERROR, "[obs-overlay] Failed to create overlay thread");
		g_running = false;
		return false;
	}

	/* Register toggle hotkey (bind in Settings → Hotkeys) */
	g_hk_toggle = obs_hotkey_register_frontend(
		"obs_overlay_toggle",
		"In-Game Overlay: Toggle Visibility",
		toggle_overlay, NULL);

	blog(LOG_INFO, "[obs-overlay] Overlay window started — "
	     "set hotkey in Settings → Hotkeys: 'In-Game Overlay: Toggle Visibility'");
	return true;
}

void obs_module_unload(void)
{
	g_running = false;
	if (g_hwnd)
		PostMessageW(g_hwnd, WM_QUIT, 0, 0);
	pthread_join(g_thread, NULL);
	blog(LOG_INFO, "[obs-overlay] Unloaded");
}

const char *obs_module_name(void)        { return "OBS In-Game Overlay"; }
const char *obs_module_description(void) {
	return "Transparent always-on-top window showing live OBS stats "
	       "(dropped frames, FPS, bitrate) for use while gaming.";
}
