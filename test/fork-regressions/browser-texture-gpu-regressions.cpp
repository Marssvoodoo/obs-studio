#include <browser-shared-texture.hpp>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
void require(bool condition, const char *message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct Texture {
	gs_texture_t *value = nullptr;
	~Texture() { gs_texture_destroy(value); }
};

struct Producer {
	ComPtr<ID3D11Texture2D> texture;
	HANDLE handle = nullptr;
	~Producer()
	{
		if (handle) {
			CloseHandle(handle);
		}
	}

	void create(ID3D11Device *device, uint32_t width, uint32_t height, bool keyed)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		/* Chromium uses SHARED + NTHANDLE for its non-mutex pooled textures. */
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
				 (keyed ? D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX : D3D11_RESOURCE_MISC_SHARED);
		require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)),
			"producer texture creation failed");
		ComPtr<IDXGIResource1> resource;
		require(SUCCEEDED(texture.As(&resource)), "producer shared resource interface failed");
		require(SUCCEEDED(resource->CreateSharedHandle(
				nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle)),
			"producer shared handle creation failed");
	}

	void fill(ID3D11DeviceContext *context, uint32_t width, uint32_t height, uint32_t color)
	{
		std::vector<uint32_t> pixels(width * height, color);
		context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), width * 4, 0);
		/* CEF invokes paint after producing a ready frame; emulate that contract. */
		ComPtr<ID3D11Device> device;
		context->GetDevice(&device);
		D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
		ComPtr<ID3D11Query> ready;
		require(SUCCEEDED(device->CreateQuery(&desc, &ready)), "producer completion query failed");
		context->End(ready.Get());
		context->Flush();
		const ULONGLONG deadline = GetTickCount64() + 5000;
		HRESULT result;
		while ((result = context->GetData(ready.Get(), nullptr, 0, 0)) == S_FALSE &&
		       GetTickCount64() < deadline) {
			Sleep(1);
		}
		require(result == S_OK, "producer frame did not finish before the callback");
	}
};

void expectPixels(gs_texture_t *texture, uint32_t color)
{
	const uint32_t width = gs_texture_get_width(texture);
	const uint32_t height = gs_texture_get_height(texture);
	auto *stage = gs_stagesurface_create(width, height, gs_texture_get_color_format(texture));
	require(stage != nullptr, "staging surface creation failed");
	gs_stage_texture(stage, texture);
	uint8_t *data = nullptr;
	uint32_t stride = 0;
	if (!gs_stagesurface_map(stage, &data, &stride)) {
		gs_stagesurface_destroy(stage);
		throw std::runtime_error("GPU readback failed");
	}
	bool matches = true;
	for (uint32_t y = 0; y < height; ++y) {
		const auto *row = reinterpret_cast<const uint32_t *>(data + y * stride);
		for (uint32_t x = 0; x < width; ++x) {
			if (matches && row[x] != color) {
				std::cerr << "Readback at " << x << ',' << y << ": expected " << std::hex << color
					  << ", got " << row[x] << std::dec << '\n';
			}
			matches = matches && row[x] == color;
		}
	}
	gs_stagesurface_unmap(stage);
	gs_stagesurface_destroy(stage);
	require(matches, "GPU pixels differ from the callback-owned frame");
}

void run()
{
	auto *consumer = static_cast<ID3D11Device *>(gs_get_device_obj());
	ComPtr<IDXGIDevice> dxgiDevice;
	require(SUCCEEDED(consumer->QueryInterface(IID_PPV_ARGS(&dxgiDevice))), "consumer DXGI device lookup failed");
	ComPtr<IDXGIAdapter> adapter;
	require(SUCCEEDED(dxgiDevice->GetAdapter(&adapter)), "consumer adapter lookup failed");
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	require(SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
					    D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device,
					    nullptr, &context)),
		"independent producer device creation failed");

	Texture owned;
	Producer producer;
	producer.create(device.Get(), 8, 8, false);
	producer.fill(context.Get(), 8, 8, 0xff113355);
	Texture borrowed;
	borrowed.value = gs_texture_open_nt_shared((uint32_t)(uintptr_t)producer.handle);
	require(borrowed.value != nullptr, "CEF-style non-mutex shared handle failed to open");
	require(CopyBrowserSharedTexture(owned.value, borrowed.value, false), "non-mutex shared frame copy failed");
	expectPixels(owned.value, 0xff113355);

	/* Negative control: the old borrowed-resource policy sees the producer's next frame. */
	producer.fill(context.Get(), 8, 8, 0xff7799bb);
	expectPixels(borrowed.value, 0xff7799bb);
	expectPixels(owned.value, 0xff113355);
	gs_texture_t *first = owned.value;
	for (uint32_t i = 0; i < 24; ++i) {
		const uint32_t color = 0xff220000 | i;
		producer.fill(context.Get(), 8, 8, color);
		require(CopyBrowserSharedTexture(owned.value, borrowed.value, false), "repeated frame copy failed");
		require(owned.value == first, "same-sized frames reallocated the destination");
		expectPixels(owned.value, color);
	}

	Producer resized;
	resized.create(device.Get(), 16, 4, false);
	resized.fill(context.Get(), 16, 4, 0xff446688);
	Texture resizedBorrow;
	resizedBorrow.value = gs_texture_open_nt_shared((uint32_t)(uintptr_t)resized.handle);
	require(CopyBrowserSharedTexture(owned.value, resizedBorrow.value, false), "resized shared frame copy failed");
	require(gs_texture_get_width(owned.value) == 16 && gs_texture_get_height(owned.value) == 4,
		"GPU destination did not resize");
	expectPixels(owned.value, 0xff446688);

	Producer keyed;
	keyed.create(device.Get(), 16, 4, true);
	ComPtr<IDXGIKeyedMutex> mutex;
	require(SUCCEEDED(keyed.texture.As(&mutex)), "producer keyed mutex lookup failed");
	require(mutex->AcquireSync(0, 0) == S_OK, "producer initial lock failed");
	keyed.fill(context.Get(), 16, 4, 0xff88aacc);
	require(SUCCEEDED(mutex->ReleaseSync(1)), "producer handoff failed");
	Texture keyedBorrow;
	keyedBorrow.value = gs_texture_open_nt_shared((uint32_t)(uintptr_t)keyed.handle);
	require(CopyBrowserSharedTexture(owned.value, keyedBorrow.value, true), "keyed shared frame copy failed");
	expectPixels(owned.value, 0xff88aacc);
	first = owned.value;
	require(!CopyBrowserSharedTexture(owned.value, keyedBorrow.value, true),
		"unavailable producer key should skip the frame");
	require(owned.value == first, "busy keyed resource discarded the previous frame");
	expectPixels(owned.value, 0xff88aacc);
	std::cout << "Real D3D11 producer reuse, 24-frame copy, resize and keyed-mutex regressions passed\n";
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "Pass the absolute built D3D11 module path\n";
		return 1;
	}
	graphics_t *graphics = nullptr;
	if (gs_create(&graphics, argv[1], 0) != GS_SUCCESS) {
		std::cerr << "Failed to initialize real D3D11 graphics\n";
		return 1;
	}
	gs_enter_context(graphics);
	int result = 0;
	try {
		run();
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		result = 1;
	}
	gs_leave_context();
	gs_destroy(graphics);
	return result;
}
