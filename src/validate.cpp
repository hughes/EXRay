// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "validate.h"

#include "histogram.h"
#include "image.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

// Determine whether a file is expected to load successfully based on its
// parent directory name.  Directories containing standard scanline, tiled,
// and single-channel images should always load through RgbaInputFile.
// Everything else (multi-part, deep, damaged, multi-view) just needs to
// not crash — a graceful error is fine.
enum class Expect { MustLoad, NoCrash };

static Expect ExpectationFor(const fs::path& filePath)
{
    for (auto p = filePath.parent_path(); p.has_filename(); p = p.parent_path())
    {
        std::string dir = p.filename().string();
        if (dir == "ScanLines" || dir == "Tiles" || dir == "TestImages" ||
            dir == "LuminanceChroma" || dir == "DisplayWindow" || dir == "Chromaticities")
            return Expect::MustLoad;
    }
    return Expect::NoCrash;
}

struct TestResult
{
    fs::path relativePath;
    bool passed = false;
    bool loaded = false;
    int width = 0;
    int height = 0;
    double loadMs = 0;
    double histMs = 0;
    std::string error;
    Expect expect = Expect::NoCrash;
};

static TestResult RunOne(const fs::path& file, const fs::path& baseDir)
{
    TestResult r;
    r.relativePath = fs::relative(file, baseDir);
    r.expect = ExpectationFor(file);

    ImageData image;
    std::string errorMsg;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = ImageLoader::LoadEXR(file.wstring(), image, errorMsg);
    auto t1 = std::chrono::steady_clock::now();
    r.loadMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (ok)
    {
        r.loaded = true;
        r.width = image.width;
        r.height = image.height;

        size_t expected = static_cast<size_t>(image.width) * image.height * 4;
        if (image.pixels.size() != expected)
        {
            r.error = "pixel buffer size mismatch";
            r.passed = (r.expect == Expect::NoCrash);
            return r;
        }

        auto h0 = std::chrono::steady_clock::now();
        HistogramComputer::Compute(image);
        auto h1 = std::chrono::steady_clock::now();
        r.histMs = std::chrono::duration<double, std::milli>(h1 - h0).count();

        r.passed = true;
    }
    else
    {
        r.error = errorMsg;
        r.passed = (r.expect == Expect::NoCrash);
    }

    return r;
}

static void WriteResult(FILE* out, const TestResult& r)
{
    fprintf(out, "%s  %-50s",
            r.passed ? "PASS" : "FAIL",
            r.relativePath.string().c_str());

    if (r.loaded)
    {
        fprintf(out, "  %5dx%-5d  load=%5.0fms  hist=%5.0fms",
                r.width, r.height, r.loadMs, r.histMs);
    }
    else
    {
        fprintf(out, "  %-12s  load=%5.0fms", "-", r.loadMs);
        if (!r.error.empty())
        {
            std::string err = r.error;
            if (err.size() > 60)
                err = err.substr(0, 57) + "...";
            fprintf(out, "  (%s)", err.c_str());
        }
    }
    fprintf(out, "\n");
}

int RunValidation(const std::wstring& path, const std::wstring& outputFile)
{
    fs::path target(path);

    // Collect files to test.
    std::vector<fs::path> files;
    fs::path baseDir;

    if (fs::is_directory(target))
    {
        baseDir = target;
        for (auto& entry : fs::recursive_directory_iterator(target))
        {
            if (entry.is_regular_file())
            {
                auto ext = entry.path().extension().string();
                if (ext == ".exr" || ext == ".EXR" || ext.empty())
                    files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
    }
    else if (fs::is_regular_file(target))
    {
        baseDir = target.parent_path();
        files.push_back(target);
    }
    else
    {
        return 1;
    }

    if (files.empty())
        return 1;

    FILE* out = _wfopen(outputFile.c_str(), L"w");
    if (!out)
        return 1;

    fprintf(out, "EXRay validation: %zu file(s)\n\n", files.size());

    int passed = 0, failed = 0;

    for (auto& f : files)
    {
        TestResult r = RunOne(f, baseDir);
        WriteResult(out, r);
        if (r.passed)
            ++passed;
        else
            ++failed;
    }

    fprintf(out, "\n");
    if (failed == 0)
        fprintf(out, "All %d test(s) passed.\n", passed);
    else
        fprintf(out, "%d of %d test(s) FAILED.\n", failed, passed + failed);

    fclose(out);
    return failed > 0 ? 1 : 0;
}
