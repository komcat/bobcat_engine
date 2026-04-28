// SwirImageSaver.cpp
#include "SwirImageSaver.h"
#include "SwirBeamOverlay.h"   // DrawBeamOverlays / DrawBeamCropStrip / DrawSingleBeamCropTile

#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {

bool GetPngEncoderClsid(CLSID& out) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

bool SavePng(Gdiplus::Bitmap& bmp, const std::wstring& path) {
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    CLSID clsid;
    if (!GetPngEncoderClsid(clsid)) return false;
    return bmp.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

}  // anonymous namespace

SwirImageSaver::SwirImageSaver(Deps deps) : m_deps(std::move(deps)) {}

bool SwirImageSaver::SaveFrameAsPng(const std::wstring& fullPath)
{
    std::vector<uint8_t> snapshot;
    uint32_t width = 0, height = 0;
    if (!m_deps.snapshotFrame || !m_deps.snapshotFrame(snapshot, width, height)) {
        return false;
    }
    if (snapshot.empty() || width == 0 || height == 0) return false;

    // The buffer is bottom-up BGR24. Point GDI+ at the last row with
    // negative stride to present a top-down view without copying again.
    const INT stride = static_cast<INT>(width) * 3;
    BYTE* scan0 = snapshot.data() + static_cast<size_t>(height - 1) * stride;
    Gdiplus::Bitmap bitmap(static_cast<INT>(width), static_cast<INT>(height),
                           -stride, PixelFormat24bppRGB, scan0);
    return SavePng(bitmap, fullPath);
}

bool SwirImageSaver::SaveFrameWithOverlayAsPng(const std::wstring& fullPath)
{
    std::vector<uint8_t> frameSnap;
    uint32_t fw = 0, fh = 0;
    if (!m_deps.snapshotFrame || !m_deps.snapshotFrame(frameSnap, fw, fh)) {
        return false;
    }
    if (frameSnap.empty() || fw == 0 || fh == 0) return false;

    std::vector<BeamDetectionResult> beamSnap =
        m_deps.snapshotBeams ? m_deps.snapshotBeams()
                              : std::vector<BeamDetectionResult>{};
    const double upp = m_deps.getMicronsPerPixel ? m_deps.getMicronsPerPixel() : 1.0;
    const bool blobMode = m_deps.getUseBlobMode ? m_deps.getUseBlobMode() : false;

    Gdiplus::Bitmap canvas(static_cast<INT>(fw), static_cast<INT>(fh),
                           PixelFormat32bppARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics gfx(&canvas);
        const INT stride = static_cast<INT>(fw) * 3;
        BYTE* scan0 = frameSnap.data() + static_cast<size_t>(fh - 1) * stride;
        Gdiplus::Bitmap rawBmp(static_cast<INT>(fw), static_cast<INT>(fh),
                                -stride, PixelFormat24bppRGB, scan0);
        gfx.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        gfx.DrawImage(&rawBmp, 0, 0,
                      static_cast<INT>(fw), static_cast<INT>(fh));

        if (!beamSnap.empty()) {
            DrawBeamOverlays(gfx, beamSnap,
                             0, 0, static_cast<int>(fw), static_cast<int>(fh),
                             fw, fh, blobMode, upp);
        }
    }
    return SavePng(canvas, fullPath);
}

bool SwirImageSaver::SaveBeamCropAsPng(int slotIndex, const std::wstring& fullPath)
{
    if (slotIndex < 0 || slotIndex > 1) return false;

    std::vector<uint8_t> frameSnap;
    uint32_t fw = 0, fh = 0;
    if (!m_deps.snapshotFrame || !m_deps.snapshotFrame(frameSnap, fw, fh)) {
        return false;
    }
    if (frameSnap.empty() || fw == 0 || fh == 0) return false;

    auto beams = m_deps.snapshotBeams ? m_deps.snapshotBeams()
                                       : std::vector<BeamDetectionResult>{};
    if (static_cast<size_t>(slotIndex) >= beams.size()) return false;

    constexpr int TILE = 200;
    Gdiplus::Bitmap canvas(TILE, TILE, PixelFormat32bppARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics gfx(&canvas);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        DrawSingleBeamCropTile(gfx, beams[slotIndex],
                               frameSnap.data(), fw, fh,
                               0, 0, TILE, slotIndex);
    }
    return SavePng(canvas, fullPath);
}

bool SwirImageSaver::SaveCollageAsPng(const std::wstring& fullPath)
{
    std::vector<uint8_t> frameSnap;
    uint32_t fw = 0, fh = 0;
    if (!m_deps.snapshotFrame || !m_deps.snapshotFrame(frameSnap, fw, fh)) {
        return false;
    }
    if (frameSnap.empty() || fw == 0 || fh == 0) return false;

    auto beamSnap = m_deps.snapshotBeams ? m_deps.snapshotBeams()
                                          : std::vector<BeamDetectionResult>{};
    const double upp = m_deps.getMicronsPerPixel ? m_deps.getMicronsPerPixel() : 1.0;
    const bool blobMode = m_deps.getUseBlobMode ? m_deps.getUseBlobMode() : false;

    constexpr int TILE = 200;
    constexpr int MARGIN = 10;
    constexpr int STRIP_BAND_H = TILE + 2 * MARGIN;          // 220
    const int rowMinW = 2 * TILE + 3 * MARGIN;                // 430
    const int collageW = std::max<int>(static_cast<int>(fw), rowMinW);
    const int collageH = static_cast<int>(fh) + STRIP_BAND_H;

    Gdiplus::Bitmap canvas(collageW, collageH, PixelFormat32bppARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics gfx(&canvas);
        Gdiplus::SolidBrush bg(Gdiplus::Color(255, 22, 22, 22));
        gfx.FillRectangle(&bg, 0, 0, collageW, collageH);

        const INT stride = static_cast<INT>(fw) * 3;
        BYTE* scan0 = frameSnap.data() + static_cast<size_t>(fh - 1) * stride;
        Gdiplus::Bitmap rawBmp(static_cast<INT>(fw), static_cast<INT>(fh),
                                -stride, PixelFormat24bppRGB, scan0);
        gfx.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        gfx.DrawImage(&rawBmp, 0, 0,
                      static_cast<INT>(fw), static_cast<INT>(fh));

        if (!beamSnap.empty()) {
            DrawBeamOverlays(gfx, beamSnap,
                             0, 0, static_cast<int>(fw), static_cast<int>(fh),
                             fw, fh, blobMode, upp);
        }

        RECT stripRect = {
            0,
            static_cast<LONG>(fh),
            collageW,
            static_cast<LONG>(fh) + STRIP_BAND_H
        };
        DrawBeamCropStrip(gfx, beamSnap, frameSnap.data(), fw, fh, stripRect);
    }
    return SavePng(canvas, fullPath);
}

bool SwirImageSaver::SaveImagePackage(const std::wstring& dirPath,
                                      const std::wstring& namePrefix,
                                      std::vector<std::wstring>* outFiles)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(dirPath), ec);

    auto join = [&](const wchar_t* suffix) {
        return (fs::path(dirPath) / (namePrefix + suffix)).wstring();
    };
    auto record = [&](bool ok, const std::wstring& path) {
        if (ok && outFiles) outFiles->push_back(path);
    };

    // Raw is the only required artifact.
    auto rawPath = join(L"_raw.png");
    bool rawOk = SaveFrameAsPng(rawPath);
    record(rawOk, rawPath);
    if (!rawOk) return false;

    auto p = join(L"_overlay.png");
    record(SaveFrameWithOverlayAsPng(p), p);
    p = join(L"_B0.png");
    record(SaveBeamCropAsPng(0, p), p);
    p = join(L"_B1.png");
    record(SaveBeamCropAsPng(1, p), p);
    p = join(L"_collage.png");
    record(SaveCollageAsPng(p), p);
    return true;
}
