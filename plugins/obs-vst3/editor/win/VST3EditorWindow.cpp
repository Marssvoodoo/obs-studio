/******************************************************************************
    Copyright (C) 2025-2026 pkv <pkv@obsproject.com>
    This file is part of obs-vst3.
    It uses the Steinberg VST3 SDK, which is licensed under MIT license.
    See https://github.com/steinbergmedia/vst3sdk for details.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "VST3EditorWindow.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <obs-module.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <windows.h>

static void logEditorError(const char *message)
{
	blog(LOG_ERROR, "[VST3 Editor] %s", message);
}

inline std::wstring utf8_to_wide(const std::string &str)
{
	if (str.empty()) {
		return std::wstring();
	}

	int sz = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), (int)str.size(), NULL, 0);
	if (sz <= 0) {
		return L"VST3 Plugin";
	}
	std::wstring out(sz, 0);
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), (int)str.size(), out.data(), sz) != sz) {
		return L"VST3 Plugin";
	}
	return out;
}

static HMODULE getCurrentModule()
{
	HMODULE hModule = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&getCurrentModule), &hModule)) {
		return GetModuleHandleW(nullptr);
	}
	return hModule;
}

class VST3EditorWindow::Impl {
public:
	Impl(Steinberg::IPlugView *view, const std::string &title)
		: view_(view),
		  hwnd_(nullptr),
		  frame_(nullptr),
		  title_(title)
	{
		resizeable_ = view && view->canResize() == Steinberg::kResultTrue;
	}

	~Impl()
	{
		if (hwnd_) {
			SetWindowLongPtr(hwnd_, GWLP_USERDATA, 0);
		}

		if (hwnd_) {
			DestroyWindow(hwnd_);
			hwnd_ = nullptr;
		}
		if (frame_) {
			delete frame_;
			frame_ = nullptr;
		}
		view_ = nullptr;
	}

	bool create(int width, int height)
	{
		static std::once_flag registrationFlag;
		static bool classRegistered = false;
		HINSTANCE hInstance = (HINSTANCE)getCurrentModule();

		std::call_once(registrationFlag, [hInstance] {
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(WNDCLASSEXW);
			wc.style = CS_DBLCLKS;
			wc.lpfnWndProc = WindowProc;
			wc.hInstance = hInstance;
			wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
			wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
			wc.lpszClassName = L"VST3EditorWin32";
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;
			if (RegisterClassExW(&wc)) {
				classRegistered = true;
				return;
			}

			WNDCLASSEXW existing = {};
			existing.cbSize = sizeof(existing);
			classRegistered = GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
					  GetClassInfoExW(hInstance, L"VST3EditorWin32", &existing) &&
					  existing.lpfnWndProc == WindowProc;
		});
		if (!classRegistered) {
			logEditorError("Plugin window class registration failed");
			return false;
		}

		DWORD exStyle = WS_EX_APPWINDOW;
		DWORD dwStyle = WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_MINIMIZEBOX;
		if (resizeable_) {
			dwStyle |= WS_SIZEBOX | WS_MAXIMIZEBOX;
		}

		std::wstring windowTitleW = utf8_to_wide(title_);

		width = std::clamp(width, 64, 8192);
		height = std::clamp(height, 64, 8192);
		RECT rect = {0, 0, width, height};
		if (!AdjustWindowRectEx(&rect, dwStyle, FALSE, exStyle)) {
			logEditorError("Plugin window frame calculation failed");
			return false;
		}

		hwnd_ = CreateWindowExW(exStyle, L"VST3EditorWin32", windowTitleW.c_str(), dwStyle, CW_USEDEFAULT,
					CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
					hInstance, nullptr);

		if (!hwnd_) {
			logEditorError("Plugin window creation failed");
			return false;
		}

		SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
		SetWindowTextW(hwnd_, windowTitleW.c_str());

		frame_ = new PlugFrameImpl(hwnd_);

		// Attach plugin view
		if (view_) {
			if (view_->setFrame(frame_) != Steinberg::kResultOk) {
				logEditorError("Plugin rejected its host frame");
				return false;
			}
			if (view_->attached((void *)hwnd_, Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk) {
				view_->setFrame(nullptr);
				logEditorError("Plugin attachment to window failed");
				return false;
			}
			// Initial size with a hack for VST3s which wait till being attached to return their size ...
			Steinberg::ViewRect vr(0, 0, width, height);
			Steinberg::ViewRect rect;
			if (view_->getSize(&rect) == Steinberg::kResultOk) {
				if (rect.getWidth() != width || rect.getHeight() != height) {
					view_->onSize(&rect);
				}
			} else {
				view_->onSize(&vr);
			}
			// DPI
			Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport> scaleSupport(view_);
			if (scaleSupport) {
				const UINT windowDpi = GetDpiForWindow(hwnd_);
				float dpi = static_cast<float>(windowDpi ? windowDpi : 96) / 96.0f;
				scaleSupport->setContentScaleFactor(dpi);
			}
		}
		return true;
	}

	void show()
	{
		if (!hwnd_ || !view_) {
			return;
		}
		SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
		wasClosed_ = false;
	}

	void hide()
	{
		if (hwnd_) {
			ShowWindow(hwnd_, SW_HIDE);
		}
	}

	class PlugFrameImpl : public Steinberg::IPlugFrame {
	public:
		PlugFrameImpl(HWND hwnd) : hwnd_(hwnd) {}
		Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView *view,
							 Steinberg::ViewRect *newSize) override
		{
			if (!hwnd_ || !view || !newSize) {
				return Steinberg::kInvalidArgument;
			}

			Steinberg::ViewRect constrained = *newSize;
			try {
				view->checkSizeConstraint(&constrained);
			} catch (...) {
				return Steinberg::kResultFalse;
			}
			const int width = std::clamp(constrained.getWidth(), 64, 8192);
			const int height = std::clamp(constrained.getHeight(), 64, 8192);

			RECT winRect = {0, 0, width, height};
			int style = GetWindowLong(hwnd_, GWL_STYLE);
			int exStyle = GetWindowLong(hwnd_, GWL_EXSTYLE);
			AdjustWindowRectEx(&winRect, style, FALSE, exStyle);
			SetWindowPos(hwnd_, nullptr, 0, 0, winRect.right - winRect.left, winRect.bottom - winRect.top,
				     SWP_NOMOVE | SWP_NOZORDER);
			return Steinberg::kResultTrue;
		}
		Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void **obj) override
		{
			if (!_iid || !obj) {
				return Steinberg::kInvalidArgument;
			}
			*obj = nullptr;
			if (Steinberg::FUnknownPrivate::iidEqual(_iid, Steinberg::IPlugFrame::iid)) {
				*obj = this;
				return Steinberg::kResultOk;
			}
			return Steinberg::kNoInterface;
		}
		// refcounting does not matter here
		uint32_t PLUGIN_API addRef() override { return 1; }
		uint32_t PLUGIN_API release() override { return 1; }

	private:
		HWND hwnd_;
	};

	bool getClosedState() { return wasClosed_; }

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		Impl *s_instance = reinterpret_cast<Impl *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		if (!s_instance) {
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		try {
			switch (msg) {
			case WM_SIZE:
				if (s_instance->view_) {
					RECT r;
					GetClientRect(hwnd, &r);
					Steinberg::ViewRect vr(0, 0, r.right - r.left, r.bottom - r.top);
					s_instance->view_->onSize(&vr);
				}
				return 0;
			case WM_SIZING: {
				if (s_instance->view_ && s_instance->resizeable_) {
					RECT *wr = reinterpret_cast<RECT *>(lParam);
					if (wr) {
						LONG style = GetWindowLong(hwnd, GWL_STYLE);
						LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

						RECT nc = {0, 0, 0, 0};
						AdjustWindowRectEx(&nc, style, FALSE, exStyle);
						const int extraWidth = (nc.right - nc.left);
						const int extraHeight = (nc.bottom - nc.top);

						const int windowWidth = wr->right - wr->left;
						const int windowHeight = wr->bottom - wr->top;

						Steinberg::ViewRect wanted(0, 0, windowWidth - extraWidth,
									   windowHeight - extraHeight);

						Steinberg::ViewRect constrained = wanted;
						s_instance->view_->checkSizeConstraint(&constrained);

						const int newWindowWidth =
							std::clamp(constrained.getWidth(), 64, 8192) + extraWidth;
						const int newWindowHeight =
							std::clamp(constrained.getHeight(), 64, 8192) + extraHeight;

						switch (wParam) {
						case WMSZ_LEFT:
						case WMSZ_TOPLEFT:
						case WMSZ_BOTTOMLEFT:
							wr->left = wr->right - newWindowWidth;
							break;
						default:
							wr->right = wr->left + newWindowWidth;
							break;
						}

						switch (wParam) {
						case WMSZ_TOP:
						case WMSZ_TOPLEFT:
						case WMSZ_TOPRIGHT:
							wr->top = wr->bottom - newWindowHeight;
							break;
						default:
							wr->bottom = wr->top + newWindowHeight;
							break;
						}

						return TRUE;
					}
				}
				break;
			}
			case WM_CLOSE:
				s_instance->hide();
				s_instance->wasClosed_ = true;
				return 0;
			case WM_DPICHANGED: {
				RECT *suggested = (RECT *)lParam;
				if (!suggested) {
					return 0;
				}
				SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
					     suggested->right - suggested->left, suggested->bottom - suggested->top,
					     SWP_NOZORDER | SWP_NOACTIVATE);

				UINT dpi = LOWORD(wParam);
				float scale = static_cast<float>(dpi) / 96.0f;

				if (s_instance->view_) {
					Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport> scaleSupport(
						s_instance->view_);
					if (scaleSupport) {
						scaleSupport->setContentScaleFactor(scale);
					}
					RECT r;
					GetClientRect(hwnd, &r);
					Steinberg::ViewRect vr(0, 0, r.right - r.left, r.bottom - r.top);
					s_instance->view_->onSize(&vr);
				}
				return 0;
			}
			}
		} catch (...) {
			logEditorError("Plugin threw while handling an editor window event");
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	Steinberg::IPlugView *view_;
	HWND hwnd_;
	PlugFrameImpl *frame_;
	std::string title_;
	bool resizeable_;
	bool wasClosed_ = false;
};

VST3EditorWindow::VST3EditorWindow(Steinberg::IPlugView *view, const std::string &title) : impl_(new Impl(view, title))
{
}

VST3EditorWindow::~VST3EditorWindow()
{
	delete impl_;
}

bool VST3EditorWindow::create(int width, int height)
{
	return impl_->create(width, height);
}

void VST3EditorWindow::show()
{
	impl_->show();
}

void VST3EditorWindow::close()
{
	impl_->hide();
}

bool VST3EditorWindow::getClosedState()
{
	return impl_->getClosedState();
}
