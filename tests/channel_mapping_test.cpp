// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for EXR channel name → RGBA display mapping.
// Build & run: bazelisk test //:channel_mapping_test

#include "image.h"

#include <cassert>
#include <cstdio>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                                                                     \
    static void test_##name();                                                                                         \
    struct Register_##name                                                                                             \
    {                                                                                                                  \
        Register_##name() { test_##name(); }                                                                           \
    } reg_##name;                                                                                                      \
    static void test_##name()

#define EXPECT(expr)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        tests_run++;                                                                                                   \
        if (expr)                                                                                                      \
        {                                                                                                              \
            tests_passed++;                                                                                            \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr);                                         \
        }                                                                                                              \
    } while (0)

// =====================================================================
// Standard RGB channels
// =====================================================================

TEST(StandardRGBA)
{
    auto m = MapChannelsToRGBA({"R", "G", "B", "A"});
    EXPECT(m.rChannel == "R");
    EXPECT(m.gChannel == "G");
    EXPECT(m.bChannel == "B");
    EXPECT(m.aChannel == "A");
    EXPECT(!m.grayscale);
}

TEST(StandardRGB_NoAlpha)
{
    auto m = MapChannelsToRGBA({"R", "G", "B"});
    EXPECT(m.rChannel == "R");
    EXPECT(m.gChannel == "G");
    EXPECT(m.bChannel == "B");
    EXPECT(m.aChannel.empty());
    EXPECT(!m.grayscale);
}

TEST(LowercaseRgba)
{
    auto m = MapChannelsToRGBA({"r", "g", "b", "a"});
    EXPECT(m.rChannel == "r");
    EXPECT(m.gChannel == "g");
    EXPECT(m.bChannel == "b");
    EXPECT(m.aChannel == "a");
    EXPECT(!m.grayscale);
}

TEST(LongNames_RedGreenBlueAlpha)
{
    auto m = MapChannelsToRGBA({"red", "green", "blue", "alpha"});
    EXPECT(m.rChannel == "red");
    EXPECT(m.gChannel == "green");
    EXPECT(m.bChannel == "blue");
    EXPECT(m.aChannel == "alpha");
    EXPECT(!m.grayscale);
}

TEST(TitleCase_RedGreenBlue)
{
    auto m = MapChannelsToRGBA({"Red", "Green", "Blue"});
    EXPECT(m.rChannel == "Red");
    EXPECT(m.gChannel == "Green");
    EXPECT(m.bChannel == "Blue");
    EXPECT(!m.grayscale);
}

// =====================================================================
// XYZ channels (color triplet)
// =====================================================================

TEST(XYZ_AllPresent_MapsToRGB)
{
    auto m = MapChannelsToRGBA({"X", "Y", "Z"});
    EXPECT(m.rChannel == "X");
    EXPECT(m.gChannel == "Y");
    EXPECT(m.bChannel == "Z");
    EXPECT(!m.grayscale);
}

TEST(XYZ_Lowercase_MapsToRGB)
{
    auto m = MapChannelsToRGBA({"x", "y", "z"});
    EXPECT(m.rChannel == "x");
    EXPECT(m.gChannel == "y");
    EXPECT(m.bChannel == "z");
    EXPECT(!m.grayscale);
}

TEST(XYZ_WithAlpha)
{
    auto m = MapChannelsToRGBA({"X", "Y", "Z", "A"});
    EXPECT(m.rChannel == "X");
    EXPECT(m.gChannel == "Y");
    EXPECT(m.bChannel == "Z");
    EXPECT(m.aChannel == "A");
    EXPECT(!m.grayscale);
}

// =====================================================================
// Lone Y channel — the Fog.exr bug case
// =====================================================================

TEST(LoneY_IsGrayscale_NotGreen)
{
    auto m = MapChannelsToRGBA({"Y"});
    EXPECT(m.rChannel.empty());
    EXPECT(m.gChannel.empty());
    EXPECT(m.bChannel.empty());
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "Y");
}

TEST(LoneY_Lowercase_IsGrayscale)
{
    auto m = MapChannelsToRGBA({"y"});
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "y");
}

TEST(XY_Only_NoZ_IsGrayscale)
{
    // X and Y without Z should NOT map to RGB — incomplete triplet
    auto m = MapChannelsToRGBA({"X", "Y"});
    EXPECT(m.rChannel.empty());
    EXPECT(m.gChannel.empty());
    EXPECT(m.bChannel.empty());
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "X"); // first channel
}

TEST(YZ_Only_NoX_IsGrayscale)
{
    auto m = MapChannelsToRGBA({"Y", "Z"});
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "Y");
}

// =====================================================================
// Single-channel grayscale
// =====================================================================

TEST(SingleChannel_Luminance)
{
    auto m = MapChannelsToRGBA({"L"});
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "L");
}

TEST(SingleChannel_Depth)
{
    auto m = MapChannelsToRGBA({"Z"});
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "Z");
}

TEST(SingleChannel_Arbitrary)
{
    auto m = MapChannelsToRGBA({"density"});
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "density");
}

// =====================================================================
// Empty channel list
// =====================================================================

TEST(EmptyChannels)
{
    auto m = MapChannelsToRGBA({});
    EXPECT(m.rChannel.empty());
    EXPECT(m.gChannel.empty());
    EXPECT(m.bChannel.empty());
    EXPECT(m.aChannel.empty());
    EXPECT(!m.grayscale);
    EXPECT(m.soloChannel.empty());
}

// =====================================================================
// Partial RGB
// =====================================================================

TEST(OnlyR_IsNotGrayscale)
{
    // R alone should map to red channel, not grayscale
    auto m = MapChannelsToRGBA({"R"});
    EXPECT(m.rChannel == "R");
    EXPECT(m.gChannel.empty());
    EXPECT(m.bChannel.empty());
    EXPECT(!m.grayscale);
}

TEST(RG_Only)
{
    auto m = MapChannelsToRGBA({"R", "G"});
    EXPECT(m.rChannel == "R");
    EXPECT(m.gChannel == "G");
    EXPECT(m.bChannel.empty());
    EXPECT(!m.grayscale);
}

TEST(RB_Only)
{
    auto m = MapChannelsToRGBA({"R", "B"});
    EXPECT(m.rChannel == "R");
    EXPECT(m.bChannel == "B");
    EXPECT(m.gChannel.empty());
    EXPECT(!m.grayscale);
}

// =====================================================================
// Mixed: RGB channels present alongside XYZ — RGB should win
// =====================================================================

TEST(RGB_And_XYZ_RGBWins)
{
    auto m = MapChannelsToRGBA({"R", "G", "B", "X", "Y", "Z"});
    EXPECT(m.rChannel == "R");
    EXPECT(m.gChannel == "G");
    EXPECT(m.bChannel == "B");
    EXPECT(!m.grayscale);
}

// =====================================================================
// Alpha-only (no color channels)
// =====================================================================

TEST(AlphaOnly_IsGrayscale)
{
    // "A" matches alpha, not a color channel — but it's still the only channel
    auto m = MapChannelsToRGBA({"A"});
    EXPECT(m.aChannel == "A");
    // With no color channels mapped, this should be grayscale
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "A");
}

// =====================================================================
// Unusual but valid channel names
// =====================================================================

TEST(MultipleUnknown_FirstIsGrayscale)
{
    auto m = MapChannelsToRGBA({"heat", "pressure"});
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "heat");
}

TEST(SingleAlpha_WithUnknown)
{
    auto m = MapChannelsToRGBA({"A", "heat"});
    EXPECT(m.aChannel == "A");
    EXPECT(m.grayscale);
    EXPECT(m.soloChannel == "A");
}

int main()
{
    printf("channel_mapping_test: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
