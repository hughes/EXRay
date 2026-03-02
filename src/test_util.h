// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Classification: should a file be expected to load, or merely not crash?
enum class Expect { MustLoad, NoCrash };

inline Expect ExpectationFor(const fs::path& filePath)
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
