// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "validate.h"

#include "histogram.h"
#include "image.h"
#include "test_util.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

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

// Synthetic test: 100 pixels (97 at luminance 0.5, 3 at luminance 8.0).
// 97th percentile falls at luminance 0.5 → log2(0.5) = -1 → autoExposure = 1.0 EV.
static TestResult RunAutoExposureTest()
{
    TestResult r;
    r.relativePath = "(synthetic) auto-exposure percentile";
    r.expect = Expect::MustLoad;

    ImageData image;
    image.width = 100;
    image.height = 1;
    image.pixels.resize(400);
    for (int i = 0; i < 100; ++i)
    {
        float v = (i < 97) ? 0.5f : 8.0f;
        float* px = &image.pixels[static_cast<size_t>(i) * 4];
        px[0] = v;
        px[1] = v;
        px[2] = v;
        px[3] = 1.0f;
    }

    HistogramData hist = HistogramComputer::Compute(image);
    if (!hist.isValid)
    {
        r.error = "histogram not valid";
        return r;
    }

    constexpr float kExpected = 1.0f;
    // Tolerance: half a histogram bin width.  300 bins over 20 stops → ~0.034 EV per half-bin.
    if (std::abs(hist.autoExposure - kExpected) > 0.04f)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "autoExposure=%.4f, expected=%.4f", hist.autoExposure, kExpected);
        r.error = buf;
        return r;
    }

    r.loaded = true;
    r.width = image.width;
    r.height = image.height;
    r.passed = true;
    return r;
}

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
        HistogramData hist = HistogramComputer::Compute(image);
        auto h1 = std::chrono::steady_clock::now();
        r.histMs = std::chrono::duration<double, std::milli>(h1 - h0).count();

        if (!hist.isValid)
        {
            r.error = "histogram not valid";
            r.passed = (r.expect == Expect::NoCrash);
            return r;
        }

        if (!std::isfinite(hist.autoExposure) || hist.autoExposure < -20.0f || hist.autoExposure > 20.0f)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "autoExposure out of range: %.2f", hist.autoExposure);
            r.error = buf;
            r.passed = false;
            return r;
        }

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
    fprintf(out, "%s  %-50s", r.passed ? "PASS" : "FAIL", r.relativePath.string().c_str());

    if (r.loaded)
    {
        fprintf(out, "  %5dx%-5d  load=%5.0fms  hist=%5.0fms", r.width, r.height, r.loadMs, r.histMs);
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
                if (ext == ".exr" || ext == ".EXR")
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

    FILE* out = _wfopen(outputFile.c_str(), L"w");
    if (!out)
        return 1;

    fprintf(out, "EXRay validation: %zu file(s)\n\n", files.size());

    int passed = 0, failed = 0;

    // Synthetic unit tests (no file I/O)
    {
        TestResult r = RunAutoExposureTest();
        WriteResult(out, r);
        if (r.passed)
            ++passed;
        else
            ++failed;
    }

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
