#include "image_loader.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#endif

namespace anitoplume
{
bool load_png_rgba(const std::string& path, ImageData& image, std::string& error)
{
#ifndef _WIN32
    error = "PNG decoding is currently implemented for Windows WIC only";
    return false;
#else
    int length = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (length <= 0) { error = "Invalid PNG path: " + path; return false; }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), length);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { error = "COM initialization failed"; return false; }
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool success = false;
    do
    {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory)))) break;
        if (FAILED(factory->CreateDecoderFromFilename(wide.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnLoad, &decoder))) break;
        if (FAILED(decoder->GetFrame(0, &frame))) break;
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom))) break;
        UINT width = 0, height = 0;
        if (FAILED(converter->GetSize(&width, &height))) break;
        image.width = width;
        image.height = height;
        image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.rgba.size()), image.rgba.data()))) break;
        success = true;
    } while (false);
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (uninitialize) CoUninitialize();
    if (!success) error = "Unable to decode PNG terrain texture: " + path;
    return success;
#endif
}
}
