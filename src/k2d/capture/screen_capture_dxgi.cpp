#include "k2d/capture/screen_capture.h"

#ifdef _WIN32

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace k2d {
namespace {

using Microsoft::WRL::ComPtr;

bool SetError(std::string *out_error, const char *msg) {
    if (out_error) {
        *out_error = msg ? msg : "unknown";
    }
    return false;
}

bool SetHrError(std::string *out_error, const char *where, HRESULT hr) {
    if (out_error) {
        char buf[256]{};
        std::snprintf(buf, sizeof(buf), "%s failed, hr=0x%08X", where ? where : "dxgi", static_cast<unsigned>(hr));
        *out_error = buf;
    }
    return false;
}

class DxgiDesktopDuplicationCapture final : public IScreenCaptureBackend {
public:
    bool Init(std::string *out_error) override {
        Shutdown();

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       flags,
                                       &fl,
                                       1,
                                       D3D11_SDK_VERSION,
                                       device_.GetAddressOf(),
                                       nullptr,
                                       context_.GetAddressOf());
        if (FAILED(hr)) {
            return SetHrError(out_error, "D3D11CreateDevice", hr);
        }

        ComPtr<IDXGIDevice> dxgi_device;
        hr = device_.As(&dxgi_device);
        if (FAILED(hr)) return SetHrError(out_error, "As(IDXGIDevice)", hr);

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgi_device->GetAdapter(adapter.GetAddressOf());
        if (FAILED(hr)) return SetHrError(out_error, "GetAdapter", hr);

        ComPtr<IDXGIOutput> output;
        hr = adapter->EnumOutputs(0, output.GetAddressOf());
        if (FAILED(hr)) return SetHrError(out_error, "EnumOutputs", hr);

        ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) return SetHrError(out_error, "As(IDXGIOutput1)", hr);

        hr = output1->DuplicateOutput(device_.Get(), duplication_.GetAddressOf());
        if (FAILED(hr)) return SetHrError(out_error, "DuplicateOutput", hr);

        DXGI_OUTDUPL_DESC desc{};
        duplication_->GetDesc(&desc);
        width_ = static_cast<int>(desc.ModeDesc.Width);
        height_ = static_cast<int>(desc.ModeDesc.Height);

        ready_ = (width_ > 0 && height_ > 0);
        if (!ready_) {
            return SetError(out_error, "invalid duplication size");
        }
        return true;
    }

    void Shutdown() noexcept override {
        ready_ = false;
        width_ = 0;
        height_ = 0;
        duplication_.Reset();
        context_.Reset();
        device_.Reset();
    }

    bool CaptureFrame(ScreenCaptureFrame &out, std::string *out_error) override {
        if (!ready_ || !duplication_ || !context_) {
            return SetError(out_error, "dxgi capture not ready");
        }

        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> frame_resource;
        HRESULT hr = duplication_->AcquireNextFrame(16, &frame_info, frame_resource.GetAddressOf());
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return false;
        }
        if (FAILED(hr)) {
            return SetHrError(out_error, "AcquireNextFrame", hr);
        }

        auto release_guard = [&]() { duplication_->ReleaseFrame(); };

        ComPtr<ID3D11Texture2D> src_tex;
        hr = frame_resource.As(&src_tex);
        if (FAILED(hr)) {
            release_guard();
            return SetHrError(out_error, "As(ID3D11Texture2D)", hr);
        }

        D3D11_TEXTURE2D_DESC src_desc{};
        src_tex->GetDesc(&src_desc);

        D3D11_TEXTURE2D_DESC staging_desc = src_desc;
        staging_desc.BindFlags = 0;
        staging_desc.MiscFlags = 0;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ComPtr<ID3D11Texture2D> staging_tex;
        hr = device_->CreateTexture2D(&staging_desc, nullptr, staging_tex.GetAddressOf());
        if (FAILED(hr)) {
            release_guard();
            return SetHrError(out_error, "CreateTexture2D(staging)", hr);
        }

        context_->CopyResource(staging_tex.Get(), src_tex.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context_->Map(staging_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            release_guard();
            return SetHrError(out_error, "Map", hr);
        }

        out.width = static_cast<int>(src_desc.Width);
        out.height = static_cast<int>(src_desc.Height);
        out.bgra.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4);

        const std::size_t row_bytes = static_cast<std::size_t>(out.width) * 4;
        const auto *src = static_cast<const std::uint8_t *>(mapped.pData);
        for (int y = 0; y < out.height; ++y) {
            std::memcpy(out.bgra.data() + static_cast<std::size_t>(y) * row_bytes,
                        src + static_cast<std::size_t>(y) * mapped.RowPitch,
                        row_bytes);
        }

        context_->Unmap(staging_tex.Get(), 0);
        release_guard();
        return true;
    }

    bool IsReady() const noexcept override { return ready_; }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;

    bool ready_ = false;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace

std::unique_ptr<IScreenCaptureBackend> CreateDxgiDesktopDuplicationCapture() {
    return std::make_unique<DxgiDesktopDuplicationCapture>();
}

}  // namespace k2d

#else

namespace k2d {

class DummyCapture final : public IScreenCaptureBackend {
public:
    bool Init(std::string *out_error) override {
        if (out_error) *out_error = "DXGI capture only supported on Windows";
        return false;
    }
    void Shutdown() noexcept override {}
    bool CaptureFrame(ScreenCaptureFrame &, std::string *out_error) override {
        if (out_error) *out_error = "DXGI capture only supported on Windows";
        return false;
    }
    bool IsReady() const noexcept override { return false; }
};

std::unique_ptr<IScreenCaptureBackend> CreateDxgiDesktopDuplicationCapture() {
    return std::make_unique<DummyCapture>();
}

}  // namespace k2d

#endif

namespace k2d {

bool ScreenCapture::Init(std::string *out_error) {
    Shutdown();
    backend_ = CreateDxgiDesktopDuplicationCapture();
    if (!backend_) {
        if (out_error) {
            *out_error = "failed to create screen capture backend";
        }
        return false;
    }
    if (!backend_->Init(out_error)) {
        backend_.reset();
        return false;
    }
    return true;
}

void ScreenCapture::Shutdown() noexcept {
    if (backend_) {
        backend_->Shutdown();
        backend_.reset();
    }
}

bool ScreenCapture::Capture(ScreenCaptureFrame &out, std::string *out_error) {
    if (!backend_) {
        if (out_error) {
            *out_error = "screen capture backend not initialized";
        }
        return false;
    }
    return backend_->CaptureFrame(out, out_error);
}

bool ScreenCapture::IsReady() const noexcept {
    return backend_ && backend_->IsReady();
}

}  // namespace k2d
