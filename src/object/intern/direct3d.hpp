#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace blur::object::direct3d {
enum class Error : uint8_t {
    kDeviceRemoved = 1u,
    kDeviceReset,
    kDeviceHung,
    kMapBufferFailed,
    kCreateBufferFailed,
    kCreateRenderTargetViewFailed,
    kCreateShaderResourceViewFailed,
    kCreateVertexShaderFailed,
    kCreatePixelShaderFailed,
    kCreateDepthStencilStateFailed,
    kCreateSamplerStateFailed,
};

std::error_code make_error_code(Error error) noexcept;

class Renderer {
  public:
    struct Affine2D {
        float row0[3uz] = {1.0f, 0.0f, 0.0f};
        float row1[3uz] = {0.0f, 1.0f, 0.0f};
    };

    struct alignas(16) Param {
        struct {
            float row0[3uz] = {1.0f, 0.0f, 0.0f};
            float padding0 = 0.0f;
            float row1[3uz] = {0.0f, 1.0f, 0.0f};
            float padding1 = 0.0f;
        } transform;
        float origin[2uz] = {0.0f, 0.0f};
        float texel[2uz] = {1.0f, 1.0f};
        float mix[2uz] = {0.0f, 1.0f};
        float decay = 1.0f;
        int32_t samples = 1;
    };

    class Context {
      public:
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        [[nodiscard]] std::error_code Draw(ID3D11Texture2D* src, const std::vector<Affine2D>& subframe_xforms,
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
    std::error_code Render(ID3D11Texture2D* dst, F&& f) {
        static_assert(std::is_same_v<std::invoke_result_t<F, Context&>, std::error_code>);

        const std::lock_guard lock(mutex_);

        if (const auto ec = Acquire(dst); ec != std::error_code{}) {
            return ec;
        }

        ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        ctx_->OMSetDepthStencilState(dss_.Get(), 0u);
        ctx_->RSSetState(nullptr);
        ctx_->GSSetShader(nullptr, nullptr, 0u);

        Context ctx(*this, dst);
        return std::forward<F>(f)(ctx);
    }

    void Reset();

  private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    [[nodiscard]] std::error_code Acquire(ID3D11Texture2D* tex);
    void Release();

    std::mutex mutex_;

    ComPtr<ID3D11Device> device_ = nullptr;
    ComPtr<ID3D11DeviceContext> ctx_ = nullptr;

    ComPtr<ID3D11DepthStencilState> dss_ = nullptr;
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

template <>
struct std::is_error_code_enum<blur::object::direct3d::Error> : true_type {};
