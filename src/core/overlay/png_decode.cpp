#include "png_decode.h"

#include <windows.h>
#include <wincodec.h>

#include <cstdio>

namespace emf::core {

namespace {

// Releases a COM pointer when it goes out of scope.
template <class T>
struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T** put() { return &p; }
    T* operator->() const { return p; }
};

std::string hr_text(const char* what, HRESULT hr) {
    char b[96];
    std::snprintf(b, sizeof b, "%s failed (0x%08lx)", what, static_cast<unsigned long>(hr));
    return b;
}

constexpr uint32_t kMaxSide = 16384;  // a sanity bound before allocating; callers apply their own, smaller limits

}  // namespace

bool decode_png(const std::wstring& file, Rgba* out, std::string* why, uint32_t max_side) {
    const auto fail = [&](std::string s) {
        if (why) *why = std::move(s);
        return false;
    };
    // COM on this thread: MTA when nothing is there yet; a thread already in an STA keeps it (WIC works in both)
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Uninit {
        bool on;
        ~Uninit() {
            if (on) CoUninitialize();
        }
    } uninit{SUCCEEDED(co)};
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) return fail(hr_text("CoInitializeEx", co));

    Com<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
    if (FAILED(hr)) return fail(hr_text("creating the WIC factory", hr));
    Com<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                            decoder.put());
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        return fail("no such file");
    if (FAILED(hr)) return fail(hr_text("reading it as a picture", hr));
    GUID container{};
    if (FAILED(decoder->GetContainerFormat(&container)) || !IsEqualGUID(container, GUID_ContainerFormatPng))
        return fail("not a PNG file");
    Com<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.put());
    if (FAILED(hr)) return fail(hr_text("reading its first frame", hr));
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0) return fail("it has no size");
    if (w > kMaxSide || h > kMaxSide || w > max_side || h > max_side)  // before any pixel is allocated
        return fail("it is too large (" + std::to_string(w) + " x " + std::to_string(h) + "; at most " +
                    std::to_string(max_side < kMaxSide ? max_side : kMaxSide) + " a side)");
    Com<IWICFormatConverter> conv;
    hr = factory->CreateFormatConverter(conv.put());
    if (FAILED(hr)) return fail(hr_text("creating a format converter", hr));
    hr = conv->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                          WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return fail(hr_text("converting it to RGBA", hr));
    Rgba r;
    r.w = w;
    r.h = h;
    r.px.resize(static_cast<size_t>(w) * h * 4);
    hr = conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(r.px.size()), r.px.data());
    if (FAILED(hr)) return fail(hr_text("copying its pixels", hr));
    *out = std::move(r);
    return true;
}

}  // namespace emf::core
