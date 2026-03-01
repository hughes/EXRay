// SPDX-License-Identifier: GPL-3.0-or-later

#include "viewport.h"

#include <cstring>

void ViewportState::ComputeTransform(float m[16]) const
{
    // Transform: image pixel coords → NDC
    // NDC: X in [-1, +1], Y in [-1, +1] (Y up in D3D NDC, but we flip for screen coords)
    //
    // Pipeline:
    //   1. Scale by zoom
    //   2. Translate by pan offset (in screen pixels)
    //   3. Map to NDC: x' = 2*x/clientWidth - 1, y' = -(2*y/clientHeight - 1)
    //      (negative Y because screen Y goes down but NDC Y goes up)

    float sx = 2.0f * zoom / clientWidth;
    float sy = -2.0f * zoom / clientHeight;
    float tx = 2.0f * panX / clientWidth - 1.0f;
    float ty = -(2.0f * panY / clientHeight - 1.0f);

    // Column-major 4x4 matrix for HLSL
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = sx;    // col 0, row 0
    m[5] = sy;    // col 1, row 1
    m[10] = 1.0f; // col 2, row 2
    m[12] = tx;   // col 3, row 0  (translation X)
    m[13] = ty;   // col 3, row 1  (translation Y)
    m[15] = 1.0f; // col 3, row 3
}

ViewportCB ViewportState::ToViewportCB() const
{
    ViewportCB cb = {};
    ComputeTransform(cb.transform);
    cb.exposure = exposure;
    cb.gamma = gamma;
    cb.zoom = zoom;
    cb.isHDR = isHDR ? 1 : 0;
    cb.sdrWhiteNits = sdrWhiteNits;
    cb.displayMaxNits = displayMaxNits;
    return cb;
}

void ViewportState::FitToWindow()
{
    if (imageWidth <= 0 || imageHeight <= 0 || clientWidth <= 0 || clientHeight <= 0)
        return;

    float scaleX = clientWidth / imageWidth;
    float scaleY = clientHeight / imageHeight;
    zoom = (std::min)(scaleX, scaleY);

    // Center the image
    panX = (clientWidth - imageWidth * zoom) * 0.5f;
    panY = (clientHeight - imageHeight * zoom) * 0.5f;
}

void ViewportState::ActualSize()
{
    if (imageWidth <= 0 || imageHeight <= 0)
        return;

    zoom = 1.0f;
    panX = (clientWidth - imageWidth) * 0.5f;
    panY = (clientHeight - imageHeight) * 0.5f;
}

void ViewportState::ZoomAt(float screenX, float screenY, float delta)
{
    float oldZoom = zoom;
    float factor = (delta > 0) ? 1.1f : (1.0f / 1.1f);
    zoom *= factor;

    // Clamp zoom to reasonable range
    zoom = (std::max)(0.01f, (std::min)(zoom, 100.0f));

    // Adjust pan so the point under the cursor stays fixed
    float ratio = zoom / oldZoom;
    panX = screenX - (screenX - panX) * ratio;
    panY = screenY - (screenY - panY) * ratio;
}

void ViewportState::AdjustExposure(float delta)
{
    exposure += delta;
    // Clamp to reasonable range
    exposure = (std::max)(-20.0f, (std::min)(exposure, 20.0f));
}

void ViewportState::AdjustGamma(float delta)
{
    gamma += delta;
    // Clamp to reasonable range (1/4.0 to 1/1.0)
    gamma = (std::max)(0.25f, (std::min)(gamma, 1.0f));
}

void ViewportState::Pan(float dx, float dy)
{
    panX += dx;
    panY += dy;
}

void ViewportState::ScreenToImage(float screenX, float screenY, float& imageX, float& imageY) const
{
    if (zoom == 0.0f)
    {
        imageX = imageY = -1.0f;
        return;
    }
    imageX = (screenX - panX) / zoom;
    imageY = (screenY - panY) / zoom;
}
