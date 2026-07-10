#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <expected>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace blur::object::direct3d {
class Renderer {
  public:
    struct SampleTransform {
        float row0[3] = {1.0f, 0.0f, 0.0f};
        float row1[3] = {0.0f, 1.0f, 0.0f};
    };

    struct alignas(16) Param {
        float base_transform_row0[3] = {1.0f, 0.0f, 0.0f};
        float padding0 = 0.0f;
        float base_transform_row1[3] = {0.0f, 1.0f, 0.0f};
        float padding1 = 0.0f;
        float pivot[2] = {0.0f, 0.0f};
        float origin[2] = {0.0f, 0.0f};
        float texel[2] = {0.0f, 0.0f};
        float amount = 0.0f;
        int32_t samples = 1;
        float mix[2] = {0.0f, 0.0f};
        float padding[2] = {0.0f, 0.0f};
    };

    using Result = std::expected<void, std::wstring>;

    class Context {
      public:
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        [[nodiscard]] Result Draw(ID3D11Texture2D* src, const std::vector<SampleTransform>& xforms,
                                  const Param& param) const;

      private:
        friend class Renderer;

        Context(Renderer& owner, ID3D11Texture2D* dst) : owner_(owner), dst_(dst) {}
        ~Context() = default;

        Renderer& owner_;
        ID3D11Texture2D* dst_;
    };

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    Renderer() = default;
    ~Renderer() = default;

    template <class F>
    Result Render(ID3D11Texture2D* dst, F&& f) {
        static_assert(std::is_same_v<std::invoke_result_t<F, Context&>, Result>);

        const std::lock_guard lock(mutex_);

        if (const auto result = Acquire(dst); !result.has_value()) {
            return result;
        }

        Context ctx(*this, dst);
        return std::forward<F>(f)(ctx);
    }

    void Reset();

  private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    [[nodiscard]] Result Acquire(ID3D11Texture2D* tex);
    void Release();

    std::mutex mutex_;

    ComPtr<ID3D11Device> device_ = nullptr;
    ComPtr<ID3D11DeviceContext> ctx_ = nullptr;

    ComPtr<ID3D11VertexShader> vs_ = nullptr;
    ComPtr<ID3D11PixelShader> ps_ = nullptr;
    ComPtr<ID3D11SamplerState> smp_ = nullptr;
    ComPtr<ID3D11Buffer> cb_ = nullptr;

    struct {
        ComPtr<ID3D11Buffer> buffer = nullptr;
        ComPtr<ID3D11ShaderResourceView> srv = nullptr;
        size_t capacity = 0uz;
    } xforms_{};
};
}  // namespace blur::object::direct3d
