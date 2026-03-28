// BeamStreamOverlay.cpp
#include "BeamStreamOverlay.h"
#include <cmath>

void BeamStreamOverlay::SetResult(const nlohmann::json& result) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult = result;
    ++m_resultSequence;
}

void BeamStreamOverlay::Clear() {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult = nlohmann::json();
    m_fps = 0.0;
}

Gdiplus::PointF BeamStreamOverlay::ImageToScreen(double imgX, double imgY,
                                                  int frameWidth, int frameHeight,
                                                  int destX, int destY,
                                                  int destWidth, int destHeight) {
    float scaleX = static_cast<float>(destWidth) / frameWidth;
    float scaleY = static_cast<float>(destHeight) / frameHeight;
    return Gdiplus::PointF(
        destX + static_cast<float>(imgX * scaleX),
        destY + static_cast<float>(imgY * scaleY)
    );
}

void BeamStreamOverlay::Draw(HDC hDC, const RECT& windowRect, int frameWidth, int frameHeight,
                              int destX, int destY, int destWidth, int destHeight,
                              double umPerPxX, double umPerPxY,
                              int displayRotation, int origFrameWidth, int origFrameHeight) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_resultMutex);

    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Info panel on TOP-RIGHT
    int panelWidth = OVERLAY_WIDTH;
    int panelX = (windowRect.right - windowRect.left) - panelWidth - OVERLAY_MARGIN;
    Gdiplus::RectF panelRect(
        static_cast<Gdiplus::REAL>(panelX),
        static_cast<Gdiplus::REAL>(OVERLAY_MARGIN),
        static_cast<Gdiplus::REAL>(panelWidth),
        static_cast<Gdiplus::REAL>(200));

    // Semi-transparent dark background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(200, 20, 20, 30));
    gfx.FillRectangle(&bgBrush, panelRect);
    Gdiplus::Pen borderPen(Gdiplus::Color(200, 80, 80, 80), 1.0f);
    gfx.DrawRectangle(&borderPen, panelRect);

    // Fonts
    Gdiplus::Font titleFont(L"Segoe UI", 11.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font labelFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular);
    Gdiplus::Font valueFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleBold);

    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 180, 180, 180));
    Gdiplus::SolidBrush valueBrush(Gdiplus::Color(255, 100, 255, 100));

    float y = panelRect.Y + 8.0f;
    float x = panelRect.X + 10.0f;
    float lineHeight = 18.0f;

    // Title
    gfx.DrawString(L"Beam Stream", -1, &titleFont, Gdiplus::PointF(x, y), &titleBrush);
    y += lineHeight + 4.0f;

    // FPS
    wchar_t fpsBuf[64];
    swprintf_s(fpsBuf, L"Streaming @ %.1f Hz", m_fps);
    gfx.DrawString(fpsBuf, -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    y += lineHeight;

    // Process time
    if (m_lastResult.contains("process_ms")) {
        wchar_t timeBuf[64];
        swprintf_s(timeBuf, L"Process: %.1f ms", m_lastResult["process_ms"].get<double>());
        gfx.DrawString(timeBuf, -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
        y += lineHeight + 4.0f;
    }

    // Per-beam info
    if (m_lastResult.contains("beams") && m_lastResult["beams"].is_array()) {
        for (const auto& beam : m_lastResult["beams"]) {
            std::string side = beam.value("side", "?");
            double cx = beam.value("centroid_x_px", 0.0);
            double cy = beam.value("centroid_y_px", 0.0);

            // Try um fields first, fall back to px fields converted to um
            double d4x = beam.value("d4sigma_x_um", 0.0);
            double d4y = beam.value("d4sigma_y_um", 0.0);
            if (d4x == 0.0 && beam.contains("d4sigma_x_px")) {
                d4x = beam["d4sigma_x_px"].get<double>() * umPerPxX;
            }
            if (d4y == 0.0 && beam.contains("d4sigma_y_px")) {
                d4y = beam["d4sigma_y_px"].get<double>() * umPerPxY;
            }

            wchar_t line1[128];
            swprintf_s(line1, L"%hs: cx=%.1f cy=%.1f px", side.c_str(), cx, cy);
            gfx.DrawString(line1, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
            y += lineHeight;

            wchar_t line2[128];
            swprintf_s(line2, L"    d4sx=%.1f d4sy=%.1f um", d4x, d4y);
            gfx.DrawString(line2, -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
            y += lineHeight + 2.0f;
        }
    }

    // Geometric overlay on image
    if (frameWidth > 0 && frameHeight > 0 && destWidth > 0 && destHeight > 0 &&
        m_lastResult.contains("beams") && m_lastResult["beams"].is_array()) {

        float scaleX = static_cast<float>(destWidth) / frameWidth;
        float scaleY = static_cast<float>(destHeight) / frameHeight;

        Gdiplus::Pen beamPen(Gdiplus::Color(255, 0, 255, 0), 2.0f);

        for (const auto& beam : m_lastResult["beams"]) {
            double cx = beam.value("centroid_x_px", 0.0);
            double cy = beam.value("centroid_y_px", 0.0);

            // Get d4sigma in pixels (prefer px fields, convert um if needed)
            double d4x_px = beam.value("d4sigma_x_px", 0.0);
            double d4y_px = beam.value("d4sigma_y_px", 0.0);
            if (d4x_px == 0.0 && umPerPxX > 0) {
                d4x_px = beam.value("d4sigma_x_um", 0.0) / umPerPxX;
            }
            if (d4y_px == 0.0 && umPerPxY > 0) {
                d4y_px = beam.value("d4sigma_y_um", 0.0) / umPerPxY;
            }

            // Transform beam coordinates from original image space to rotated display space
            if (displayRotation != 0 && origFrameWidth > 0) {
                if (displayRotation == -90) {
                    // -90 CCW: (x, y) -> (y, W_orig - 1 - x)
                    double newCx = cy;
                    double newCy = static_cast<double>(origFrameWidth - 1) - cx;
                    cx = newCx;
                    cy = newCy;
                }
                else if (displayRotation == 90) {
                    // +90 CW: (x, y) -> (H_orig - 1 - y, x)
                    double newCx = static_cast<double>(origFrameHeight - 1) - cy;
                    double newCy = cx;
                    cx = newCx;
                    cy = newCy;
                }
                // Swap d4sigma axes (rotation swaps X and Y)
                std::swap(d4x_px, d4y_px);
            }

            Gdiplus::PointF center = ImageToScreen(cx, cy,
                frameWidth, frameHeight, destX, destY, destWidth, destHeight);

            // Crosshair at centroid
            float crossSize = 12.0f;
            gfx.DrawLine(&beamPen, center.X - crossSize, center.Y, center.X + crossSize, center.Y);
            gfx.DrawLine(&beamPen, center.X, center.Y - crossSize, center.X, center.Y + crossSize);

            // Ellipse sized by d4sigma in pixels, scaled to screen
            {
                float ellipseW = static_cast<float>(d4x_px * scaleX);
                float ellipseH = static_cast<float>(d4y_px * scaleY);
                if (ellipseW > 2.0f && ellipseH > 2.0f) {
                    gfx.DrawEllipse(&beamPen,
                        center.X - ellipseW / 2.0f, center.Y - ellipseH / 2.0f,
                        ellipseW, ellipseH);
                }
            }
        }
    }
}
