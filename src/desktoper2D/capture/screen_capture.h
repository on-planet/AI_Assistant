#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace desktoper2D {

struct ScreenCaptureFrame {
    int width = 0;
    int height = 0;
    std::shared_ptr<std::vector<std::uint8_t>> bgra;

    bool HasPixels() const noexcept {
        return bgra && !bgra->empty();
    }

    const std::uint8_t *Data() const noexcept {
        return HasPixels() ? bgra->data() : nullptr;
    }

    std::uint8_t *MutableData() noexcept {
        return HasPixels() ? bgra->data() : nullptr;
    }

    std::size_t ByteSize() const noexcept {
        return bgra ? bgra->size() : 0;
    }

    void EnsureBgraSize(const std::size_t total_bytes) {
        if (!bgra) {
            bgra = std::make_shared<std::vector<std::uint8_t>>(total_bytes);
            return;
        }
        if (bgra.use_count() > 1) {
            bgra = std::make_shared<std::vector<std::uint8_t>>(total_bytes);
            return;
        }
        if (bgra->size() != total_bytes) {
            bgra->resize(total_bytes);
        }
    }
};

class IScreenCaptureBackend {
public:
    virtual ~IScreenCaptureBackend() = default;
    virtual bool Init(std::string *out_error) = 0;
    virtual void Shutdown() noexcept = 0;
    virtual bool CaptureFrame(ScreenCaptureFrame &out, std::string *out_error) = 0;
    virtual bool IsReady() const noexcept = 0;
};

// 统一截图接口：上层仅依赖该类型，不直接感知具体后端。
class ScreenCapture {
public:
    ScreenCapture() = default;

    // Win10/11 下默认使用 DXGI Desktop Duplication。
    bool Init(std::string *out_error = nullptr);
    void Shutdown() noexcept;

    // 成功抓到一帧时返回 true；超时/暂无新帧返回 false 且 out_error 为空。
    bool Capture(ScreenCaptureFrame &out, std::string *out_error = nullptr);

    bool IsReady() const noexcept;

private:
    std::unique_ptr<IScreenCaptureBackend> backend_;
};

std::unique_ptr<IScreenCaptureBackend> CreateDxgiDesktopDuplicationCapture();

}  // namespace desktoper2D
