// VisionResultOverlay.cpp

#ifdef HAS_OPENCV

#include "VisionResultOverlay.h"
#include <cmath>

void VisionResultOverlay::SetResult(const VisionResult& result, const std::string& profileName) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult = result;
    m_profileName = profileName;
    m_resultSequence.fetch_add(1, std::memory_order_relaxed);
}

void VisionResultOverlay::Clear() {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_lastResult.clear();
    m_profileName.clear();
}

Gdiplus::PointF VisionResultOverlay::ImageToScreen(double imgX, double imgY,
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

void VisionResultOverlay::Draw(HDC hDC, const RECT& /*windowRect*/, int frameWidth, int frameHeight,
                                int destX, int destY, int destWidth, int destHeight) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_resultMutex);

    if (m_profileName.empty()) return;

    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Draw info panel on LEFT side
    Gdiplus::RectF panelRect(
        static_cast<Gdiplus::REAL>(OVERLAY_MARGIN),
        static_cast<Gdiplus::REAL>(OVERLAY_MARGIN),
        static_cast<Gdiplus::REAL>(OVERLAY_WIDTH),
        static_cast<Gdiplus::REAL>(300));  // Dynamic height based on content

    DrawInfoPanel(gfx, panelRect);

    // Draw geometric overlays on the camera image
    if (frameWidth > 0 && frameHeight > 0 && destWidth > 0 && destHeight > 0) {
        DrawGeometricOverlays(hDC, frameWidth, frameHeight, destX, destY, destWidth, destHeight);
    }
}

void VisionResultOverlay::DrawInfoPanel(Gdiplus::Graphics& gfx, const Gdiplus::RectF& panelRect) {
    // Semi-transparent dark background
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(200, 20, 20, 30));
    gfx.FillRectangle(&bgBrush, panelRect);

    // Border
    Gdiplus::Pen borderPen(Gdiplus::Color(200, 80, 80, 80), 1.0f);
    gfx.DrawRectangle(&borderPen, panelRect);

    // Fonts
    Gdiplus::Font titleFont(L"Segoe UI", 11.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font labelFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleRegular);
    Gdiplus::Font valueFont(L"Segoe UI", 9.0f, Gdiplus::FontStyleBold);

    // Colors
    Gdiplus::SolidBrush titleBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 180, 180, 180));
    Gdiplus::SolidBrush successBrush(Gdiplus::Color(255, 100, 255, 100));
    Gdiplus::SolidBrush errorBrush(Gdiplus::Color(255, 255, 100, 100));
    Gdiplus::SolidBrush valueBrush(Gdiplus::Color(255, 0, 200, 255));

    float y = panelRect.Y + 8.0f;
    float x = panelRect.X + 10.0f;
    float lineHeight = 18.0f;

    // Title
    gfx.DrawString(L"Vision Result", -1, &titleFont, Gdiplus::PointF(x, y), &titleBrush);
    y += lineHeight + 4.0f;

    // Profile name
    std::wstring wProfile(m_profileName.begin(), m_profileName.end());
    if (wProfile.length() > 28) {
        wProfile = wProfile.substr(0, 25) + L"...";
    }
    gfx.DrawString(wProfile.c_str(), -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    y += lineHeight + 8.0f;

    // Status
    std::wstring statusStr = m_lastResult.success ? L"SUCCESS" : L"FAILED";
    Gdiplus::SolidBrush* statusBrush = m_lastResult.success ? &successBrush : &errorBrush;
    gfx.DrawString(L"Status: ", -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    gfx.DrawString(statusStr.c_str(), -1, &valueFont, Gdiplus::PointF(x + 50.0f, y), statusBrush);
    y += lineHeight;

    // Processing time
    wchar_t timeBuf[32];
    swprintf_s(timeBuf, L"%.1f ms", m_lastResult.processingTimeMs);
    gfx.DrawString(L"Time: ", -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
    gfx.DrawString(timeBuf, -1, &valueFont, Gdiplus::PointF(x + 50.0f, y), &valueBrush);
    y += lineHeight + 8.0f;

    // Detection counts
    if (!m_lastResult.circles.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Circles: %zu", m_lastResult.circles.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    if (!m_lastResult.points.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Points: %zu", m_lastResult.points.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    if (!m_lastResult.lines.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Lines: %zu", m_lastResult.lines.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    if (!m_lastResult.beams.empty()) {
        wchar_t buf[32];
        swprintf_s(buf, L"Beams: %zu", m_lastResult.beams.size());
        gfx.DrawString(buf, -1, &valueFont, Gdiplus::PointF(x, y), &valueBrush);
        y += lineHeight;
    }

    // Measurements
    if (!m_lastResult.measurements.empty()) {
        y += 4.0f;
        for (const auto& [key, val] : m_lastResult.measurements) {
            std::wstring wKey(key.begin(), key.end());
            if (wKey.length() > 15) {
                wKey = wKey.substr(0, 12) + L"...";
            }
            wchar_t valBuf[32];
            swprintf_s(valBuf, L"%.4f", val);
            gfx.DrawString(wKey.c_str(), -1, &labelFont, Gdiplus::PointF(x, y), &dimBrush);
            gfx.DrawString(valBuf, -1, &valueFont, Gdiplus::PointF(x + 100.0f, y), &valueBrush);
            y += lineHeight;

            // Limit display
            if (y > panelRect.Y + panelRect.Height - 20.0f) break;
        }
    }
}

void VisionResultOverlay::DrawGeometricOverlays(HDC hDC, int frameWidth, int frameHeight,
                                                 int destX, int destY, int destWidth, int destHeight) {
    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    float scaleX = static_cast<float>(destWidth) / frameWidth;
    float scaleY = static_cast<float>(destHeight) / frameHeight;

    // Draw circles (green outline + center crosshair)
    Gdiplus::Pen circlePen(Gdiplus::Color(255, 0, 255, 0), 2.0f);
    for (const auto& c : m_lastResult.circles) {
        Gdiplus::PointF center = ImageToScreen(c.centerPx.x, c.centerPx.y,
                                                frameWidth, frameHeight,
                                                destX, destY, destWidth, destHeight);
        float radius = static_cast<float>(c.radiusPx * scaleX);

        // Draw circle
        gfx.DrawEllipse(&circlePen,
                        center.X - radius, center.Y - radius,
                        radius * 2.0f, radius * 2.0f);

        // Draw crosshair at center
        float crossSize = 8.0f;
        gfx.DrawLine(&circlePen, center.X - crossSize, center.Y, center.X + crossSize, center.Y);
        gfx.DrawLine(&circlePen, center.X, center.Y - crossSize, center.X, center.Y + crossSize);
    }

    // Draw points (yellow filled circles)
    Gdiplus::SolidBrush pointBrush(Gdiplus::Color(255, 255, 255, 0));
    Gdiplus::Pen pointPen(Gdiplus::Color(255, 255, 200, 0), 1.0f);
    for (const auto& pt : m_lastResult.points) {
        Gdiplus::PointF screenPt = ImageToScreen(pt.pixelCoord.x, pt.pixelCoord.y,
                                                  frameWidth, frameHeight,
                                                  destX, destY, destWidth, destHeight);
        float radius = 5.0f;
        gfx.FillEllipse(&pointBrush,
                        screenPt.X - radius, screenPt.Y - radius,
                        radius * 2.0f, radius * 2.0f);
        gfx.DrawEllipse(&pointPen,
                        screenPt.X - radius, screenPt.Y - radius,
                        radius * 2.0f, radius * 2.0f);
    }

    // Draw lines (cyan)
    Gdiplus::Pen linePen(Gdiplus::Color(255, 0, 255, 255), 2.0f);
    for (const auto& line : m_lastResult.lines) {
        Gdiplus::PointF start = ImageToScreen(line.startPx.x, line.startPx.y,
                                               frameWidth, frameHeight,
                                               destX, destY, destWidth, destHeight);
        Gdiplus::PointF end = ImageToScreen(line.endPx.x, line.endPx.y,
                                             frameWidth, frameHeight,
                                             destX, destY, destWidth, destHeight);
        gfx.DrawLine(&linePen, start, end);

        // Draw endpoints
        float epRadius = 4.0f;
        gfx.DrawEllipse(&linePen, start.X - epRadius, start.Y - epRadius, epRadius * 2.0f, epRadius * 2.0f);
        gfx.DrawEllipse(&linePen, end.X - epRadius, end.Y - epRadius, epRadius * 2.0f, epRadius * 2.0f);
    }

    // Draw beams (magenta ellipse)
    Gdiplus::Pen beamPen(Gdiplus::Color(255, 255, 0, 255), 2.0f);
    for (const auto& beam : m_lastResult.beams) {
        Gdiplus::PointF center = ImageToScreen(beam.centerPx.x, beam.centerPx.y,
                                                frameWidth, frameHeight,
                                                destX, destY, destWidth, destHeight);
        float majorAxis = static_cast<float>(beam.fwhmMajorPx * scaleX);
        float minorAxis = static_cast<float>(beam.fwhmMinorPx * scaleY);

        // Draw ellipse (approximate - no rotation in GDI+)
        gfx.DrawEllipse(&beamPen,
                        center.X - majorAxis / 2.0f, center.Y - minorAxis / 2.0f,
                        majorAxis, minorAxis);

        // Draw center crosshair
        float crossSize = 10.0f;
        gfx.DrawLine(&beamPen, center.X - crossSize, center.Y, center.X + crossSize, center.Y);
        gfx.DrawLine(&beamPen, center.X, center.Y - crossSize, center.X, center.Y + crossSize);
    }
}

#endif // HAS_OPENCV
