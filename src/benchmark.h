// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

// Run headless benchmark against a directory of EXR files.
// Loads each MustLoad file `iterations` times, computes median timings,
// writes structured JSON to outputFile.
// Returns 0 on success, 1 on error (e.g. no files found, can't write output).
int RunBenchmark(const std::wstring& path, const std::wstring& outputFile, int iterations = 5);
