// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "benchmark.h"

#include "histogram.h"
#include "image.h"
#include "test_util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

struct BenchmarkResult
{
    fs::path relativePath;
    int width = 0;
    int height = 0;
    double megapixels = 0.0;
    bool loaded = false;
    std::string error;

    std::vector<double> loadSamples;
    std::vector<double> histSamples;

    double medianLoadMs = 0.0;
    double medianHistMs = 0.0;
    double loadMpxPerSec = 0.0;
    double histMpxPerSec = 0.0;
};

static double Median(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1)
        return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

static BenchmarkResult BenchmarkOne(const fs::path& file, const fs::path& baseDir, int iterations)
{
    BenchmarkResult r;
    r.relativePath = fs::relative(file, baseDir);

    // Warmup run — primes file system cache and code paths.
    {
        ImageData warmupImage;
        std::string warmupErr;
        bool ok = ImageLoader::LoadEXR(file.wstring(), warmupImage, warmupErr);
        if (!ok)
        {
            r.error = warmupErr;
            return r;
        }
        r.width = warmupImage.width;
        r.height = warmupImage.height;
        r.megapixels = static_cast<double>(r.width) * r.height / 1e6;
        r.loaded = true;

        HistogramComputer::Compute(warmupImage);
    }

    // Timed iterations.
    for (int i = 0; i < iterations; ++i)
    {
        ImageData image;
        std::string errorMsg;

        auto t0 = std::chrono::steady_clock::now();
        bool ok = ImageLoader::LoadEXR(file.wstring(), image, errorMsg);
        auto t1 = std::chrono::steady_clock::now();

        r.loadSamples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

        if (ok)
        {
            auto h0 = std::chrono::steady_clock::now();
            HistogramComputer::Compute(image);
            auto h1 = std::chrono::steady_clock::now();

            r.histSamples.push_back(std::chrono::duration<double, std::milli>(h1 - h0).count());
        }
    }

    r.medianLoadMs = Median(r.loadSamples);
    r.medianHistMs = Median(r.histSamples);

    if (r.medianLoadMs > 0.0)
        r.loadMpxPerSec = r.megapixels / (r.medianLoadMs / 1000.0);
    if (r.medianHistMs > 0.0)
        r.histMpxPerSec = r.megapixels / (r.medianHistMs / 1000.0);

    return r;
}

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static void WriteJsonResults(FILE* out, const std::vector<BenchmarkResult>& results, int iterations)
{
    double totalMegapixels = 0.0;
    double totalLoadMs = 0.0;
    double totalHistMs = 0.0;
    int filesLoaded = 0;

    fprintf(out, "{\n");
    fprintf(out, "  \"version\": 1,\n");
    fprintf(out, "  \"iterations\": %d,\n", iterations);
    fprintf(out, "  \"files\": [\n");

    for (size_t i = 0; i < results.size(); ++i)
    {
        const auto& r = results[i];
        fprintf(out, "    {\n");
        fprintf(out, "      \"path\": \"%s\",\n", JsonEscape(r.relativePath.string()).c_str());

        if (r.loaded)
        {
            fprintf(out, "      \"width\": %d,\n", r.width);
            fprintf(out, "      \"height\": %d,\n", r.height);
            fprintf(out, "      \"megapixels\": %.3f,\n", r.megapixels);
            fprintf(out, "      \"median_load_ms\": %.2f,\n", r.medianLoadMs);
            fprintf(out, "      \"median_hist_ms\": %.2f,\n", r.medianHistMs);
            fprintf(out, "      \"load_mpx_per_sec\": %.1f,\n", r.loadMpxPerSec);
            fprintf(out, "      \"hist_mpx_per_sec\": %.1f\n", r.histMpxPerSec);

            totalMegapixels += r.megapixels;
            totalLoadMs += r.medianLoadMs;
            totalHistMs += r.medianHistMs;
            ++filesLoaded;
        }
        else
        {
            fprintf(out, "      \"error\": \"%s\"\n", JsonEscape(r.error).c_str());
        }

        fprintf(out, "    }%s\n", (i + 1 < results.size()) ? "," : "");
    }

    fprintf(out, "  ],\n");

    double loadMpxPerSec = (totalLoadMs > 0.0) ? totalMegapixels / (totalLoadMs / 1000.0) : 0.0;
    double histMpxPerSec = (totalHistMs > 0.0) ? totalMegapixels / (totalHistMs / 1000.0) : 0.0;

    fprintf(out, "  \"summary\": {\n");
    fprintf(out, "    \"total_files\": %zu,\n", results.size());
    fprintf(out, "    \"files_loaded\": %d,\n", filesLoaded);
    fprintf(out, "    \"total_megapixels\": %.3f,\n", totalMegapixels);
    fprintf(out, "    \"total_load_ms\": %.2f,\n", totalLoadMs);
    fprintf(out, "    \"total_hist_ms\": %.2f,\n", totalHistMs);
    fprintf(out, "    \"load_mpx_per_sec\": %.1f,\n", loadMpxPerSec);
    fprintf(out, "    \"hist_mpx_per_sec\": %.1f\n", histMpxPerSec);
    fprintf(out, "  }\n");
    fprintf(out, "}\n");
}

int RunBenchmark(const std::wstring& path, const std::wstring& outputFile, int iterations)
{
    fs::path target(path);

    // Collect benchmarkable files (MustLoad only — timing failures is meaningless).
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
                if ((ext == ".exr" || ext == ".EXR") && ExpectationFor(entry.path()) == Expect::MustLoad)
                {
                    files.push_back(entry.path());
                }
            }
        }
        std::sort(files.begin(), files.end());
    }
    else if (fs::is_regular_file(target))
    {
        baseDir = target.parent_path();
        files.push_back(target);
    }

    if (files.empty())
        return 1;

    std::vector<BenchmarkResult> results;
    results.reserve(files.size());

    for (auto& f : files)
        results.push_back(BenchmarkOne(f, baseDir, iterations));

    FILE* out = _wfopen(outputFile.c_str(), L"w");
    if (!out)
        return 1;

    WriteJsonResults(out, results, iterations);
    fclose(out);
    return 0;
}
