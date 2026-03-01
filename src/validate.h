// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

// Run headless validation against a single EXR file or a directory of them.
// Results are written to outputFile. Returns 0 if all passed, 1 if any failed.
int RunValidation(const std::wstring& path, const std::wstring& outputFile);
