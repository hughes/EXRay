#pragma once

#include "renderer.h"

#include <algorithm>
#include <cmath>

struct ViewportState
{
    // Image dimensions (set when image loads)
    float imageWidth = 0;
    float imageHeight = 0;

    // View state
    float panX = 0; // Pan offset in screen pixels
    float panY = 0;
    float zoom = 1.0f;     // 1.0 = 1 image pixel = 1 screen pixel
    float exposure = 0.0f; // EV stops
    float gamma = 1.0f / 2.2f;
    bool isHDR = false;
    float sdrWhiteNits = 80.0f;
    float displayMaxNits = 80.0f;

    // Client area
    float clientWidth = 0;
    float clientHeight = 0;

    // Compute 4x4 column-major transform: image pixels → NDC
    void ComputeTransform(float outMatrix[16]) const;

    // Fill a ViewportCB struct for the renderer
    ViewportCB ToViewportCB() const;

    // Fit image to window (maintain aspect ratio, centered)
    void FitToWindow();

    // Actual size (1:1 pixel mapping, centered)
    void ActualSize();

    // Zoom centered on screen point (cursor position)
    void ZoomAt(float screenX, float screenY, float delta);

    // Adjust exposure in EV stops
    void AdjustExposure(float delta);

    // Adjust gamma exponent
    void AdjustGamma(float delta);

    // Pan by screen pixel delta
    void Pan(float dx, float dy);

    // Convert screen coordinates to image coordinates
    void ScreenToImage(float screenX, float screenY, float& imageX, float& imageY) const;
};
