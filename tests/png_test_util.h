#pragma once
// Tests' helper: writes a PNG with WIC's own encoder (pictures in mod windows, API v26).
#include <windows.h>
#include <wincodec.h>

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace emf::test {

// A PNG of w x h RGBA8 pixels, row by row.
inline bool write_png(const std::filesystem::path& file, UINT w, UINT h, const std::vector<uint8_t>& rgba) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))))
        return false;
    IWICStream* s = nullptr;
    IWICBitmapEncoder* e = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    bool ok = SUCCEEDED(f->CreateStream(&s)) && SUCCEEDED(s->InitializeFromFilename(file.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e)) &&
              SUCCEEDED(e->Initialize(s, WICBitmapEncoderNoCache)) && SUCCEEDED(e->CreateNewFrame(&fr, nullptr)) &&
              SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(w, h));
    std::vector<uint8_t> bgra = rgba;  // the PNG encoder takes BGRA
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    ok = ok && SUCCEEDED(fr->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
         SUCCEEDED(fr->WritePixels(h, w * 4, static_cast<UINT>(bgra.size()), bgra.data())) &&
         SUCCEEDED(fr->Commit()) && SUCCEEDED(e->Commit());
    if (fr) fr->Release();
    if (e) e->Release();
    if (s) s->Release();
    f->Release();
    return ok;
}

// A w x h picture of one colour.
inline std::vector<uint8_t> solid(UINT w, UINT h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

}  // namespace emf::test
