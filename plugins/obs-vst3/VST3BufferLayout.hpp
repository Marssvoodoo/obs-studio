/******************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <cstddef>
#include <limits>

inline bool vst3_output_float_count(size_t frames, size_t channels, size_t &count) noexcept
{
	if (channels != 0 && frames > std::numeric_limits<size_t>::max() / channels) {
		return false;
	}
	count = frames * channels;
	return true;
}

inline size_t vst3_output_channel_offset(size_t frames, size_t channel) noexcept
{
	return frames * channel;
}
