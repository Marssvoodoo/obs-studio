#include <browser-shared-texture.hpp>

#include <algorithm>
#include <cerrno>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct gs_texture {
	uint32_t width;
	uint32_t height;
	gs_color_format format;
	std::shared_ptr<std::vector<uint8_t>> pixels;
};

namespace {
std::vector<std::string> calls;
bool failAllocation = false;
int acquireResult = 0;
int liveTextures = 0;
int failures = 0;

void check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << message << '\n';
		++failures;
	}
}
} // namespace

extern "C" {
uint32_t gs_texture_get_width(const gs_texture_t *texture)
{
	return texture->width;
}
uint32_t gs_texture_get_height(const gs_texture_t *texture)
{
	return texture->height;
}
gs_color_format gs_texture_get_color_format(const gs_texture_t *texture)
{
	return texture->format;
}

gs_texture_t *gs_texture_create(uint32_t width, uint32_t height, gs_color_format format, uint32_t, const uint8_t **,
				uint32_t)
{
	calls.emplace_back("create");
	if (failAllocation) {
		return nullptr;
	}
	++liveTextures;
	return new gs_texture{width, height, format, std::make_shared<std::vector<uint8_t>>(width * height * 4)};
}

void gs_texture_destroy(gs_texture_t *texture)
{
	if (texture) {
		calls.emplace_back("destroy");
		--liveTextures;
		delete texture;
	}
}

int gs_texture_acquire_sync(gs_texture_t *, uint64_t key, uint32_t timeout)
{
	check(key == 1 && timeout == 0, "mutex acquisition must use the producer key without blocking");
	calls.emplace_back("acquire");
	return acquireResult;
}

int gs_texture_release_sync(gs_texture_t *, uint64_t key)
{
	check(key == 0, "mutex release must return the producer key");
	calls.emplace_back("release");
	return 0;
}

void gs_copy_texture(gs_texture_t *destination, gs_texture_t *source)
{
	calls.emplace_back("copy");
	*destination->pixels = *source->pixels;
}

void gs_flush()
{
	calls.emplace_back("flush");
}
}

int main()
{
	gs_texture_t *owned = nullptr;
	auto *pooled = gs_texture_create(4, 3, GS_BGRA, 1, nullptr, 0);
	std::fill(pooled->pixels->begin(), pooled->pixels->end(), 17);
	calls.clear();
	check(CopyBrowserSharedTexture(owned, pooled, false), "non-mutex CEF frame must copy successfully");
	check(calls == std::vector<std::string>{"create", "copy", "flush"}, "non-mutex frames must not acquire sync");
	check(owned != pooled && owned->pixels != pooled->pixels, "the displayed frame must own independent storage");
	std::fill(pooled->pixels->begin(), pooled->pixels->end(), 42);
	check(owned->pixels->front() == 17,
	      "reusing CEF's texture after callback return must not change the displayed frame");
	gs_texture_t *first = owned;
	calls.clear();
	check(CopyBrowserSharedTexture(owned, pooled, false), "same-sized next frame must copy");
	check(owned == first && owned->pixels->front() == 42, "same-size frames must reuse the owned destination");
	check(calls == std::vector<std::string>{"copy", "flush"}, "unchanged dimensions/format must not reallocate");

	acquireResult = ETIMEDOUT;
	calls.clear();
	check(!CopyBrowserSharedTexture(owned, pooled, true), "a busy keyed texture must be skipped");
	check(owned == first && calls == std::vector<std::string>{"acquire"},
	      "mutex timeout must retain the last frame");
	acquireResult = -1;
	calls.clear();
	check(!CopyBrowserSharedTexture(owned, pooled, true), "mutex errors must fail closed");
	check(calls == std::vector<std::string>{"acquire"},
	      "mutex errors must not copy, flush or release unowned locks");
	acquireResult = 0;
	calls.clear();
	check(CopyBrowserSharedTexture(owned, pooled, true), "an available keyed texture must copy");
	check(calls == std::vector<std::string>{"acquire", "copy", "flush", "release"},
	      "copy must be submitted before mutex release");

	auto *resized = gs_texture_create(8, 2, GS_BGRA, 1, nullptr, 0);
	std::fill(resized->pixels->begin(), resized->pixels->end(), 63);
	failAllocation = true;
	calls.clear();
	check(!CopyBrowserSharedTexture(owned, resized, true), "allocation failures must skip the new frame");
	check(owned == first && owned->width == 4, "allocation failure must retain the previous owned frame");
	check(calls == std::vector<std::string>{"acquire", "create", "release"},
	      "failed allocation must release the acquired mutex");
	failAllocation = false;
	calls.clear();
	check(CopyBrowserSharedTexture(owned, resized, false), "resize must allocate a matching destination");
	check(owned->width == 8 && owned->height == 2 && owned->pixels->front() == 63,
	      "resized frame must be complete");
	check(calls == std::vector<std::string>{"create", "copy", "flush", "destroy"},
	      "previous frame must survive until replacement is submitted");
	resized->format = GS_RGBA;
	calls.clear();
	check(CopyBrowserSharedTexture(owned, resized, false), "format changes must allocate a matching destination");
	check(owned->format == GS_RGBA && calls.front() == "create", "destination format must follow the producer");

	first = owned;
	calls.clear();
	check(!CopyBrowserSharedTexture(owned, nullptr, false),
	      "invalid/open-failed handles must retain the last frame");
	check(owned == first && calls.empty(), "invalid handles must not touch graphics resources");
	resized->width = 0;
	calls.clear();
	check(!CopyBrowserSharedTexture(owned, resized, true), "empty shared frames must be rejected");
	check(owned == first && calls == std::vector<std::string>{"acquire", "release"},
	      "empty frame must release sync without modifying the display");

	gs_texture_destroy(pooled);
	gs_texture_destroy(resized);
	gs_texture_destroy(owned);
	check(liveTextures == 0, "all shared wrappers and owned textures must be released");
	if (!failures) {
		std::cout << "Browser pooled texture ownership and failure regressions passed\n";
	}
	return failures ? 1 : 0;
}
