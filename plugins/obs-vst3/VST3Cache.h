/******************************************************************************
    Copyright (C) 2026 OBS contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

class VST3Scanner;

bool vst3_list_load_json(VST3Scanner &scanner, const char *path, bool safeBackup, bool includeDiscardable);
bool vst3_list_save_json(const VST3Scanner &scanner, const char *path, const char *backupExtension,
			 bool includeDiscardable);
