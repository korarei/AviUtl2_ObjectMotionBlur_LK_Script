#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#ifdef NOMINMAX
#undef NOMINMAX
#endif
#include <NvOFD3D11.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <cstdint>
#include <cstddef>
#include <expected>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace blur::scene {
enum class Error : uint8_t {
    kDeviceRemoved = 1u,
    kDeviceReset,
    kDeviceHung,
    kMapBufferFailed,
    kCreateBufferFailed,
    kCreateTexture2DFailed,
    kCreateRenderTargetViewFailed,
    kCreateShaderResourceViewFailed,
    kCreateUnorderedAccessViewFailed,
    kCreateVertexShaderFailed,
    kCreatePixelShaderFailed,
    kCreateComputeShaderFailed,
    kCreateDepthStencilStateFailed,
    kCreateSamplerStateFailed,
    kOpticalFlowUnavailable,
    kInvalidOpticalFlowCall,
    kFlowAlreadyConsumed,
    kIncompatibleOpticalFlowVersion,
    kOpticalFlowNotInitialized,
    kOpticalFlowInternalError,
    kOpticalFlowDirect3DFailed,
    kUnknownOpticalFlowException,
    kNoSupportedOutputGridSize,
    kRegisterOpticalFlowInputTextureFailed,
};

std::error_code make_error_code(Error error) noexcept;

class Renderer {
  public:
    struct Source {
        std::array<ID3D11Texture2D*, 2uz> inputs{};
        ID3D11Texture2D* depth = nullptr;
    };

    struct ID {
        uint16_t w = 0u, h = 0u;
        int32_t preset = 0;
    };

    struct Param {
        ID id{};
        bool should_use_temporal_hints = false;
        float scale = 1.0f;
        float amount = 1.0f;
        float phase = 0.0f;
        int32_t sample_limit = 1;
        float mix = 1.0f;
        float falloff = 0.0f;
        int32_t view_mode = 0;
    };

    class Context {
      public:
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        [[nodiscard]] std::error_code Draw(const Source& src, const Param& param) const;

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

        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ctx_->VSSetShader(vs_.Get(), nullptr, 0u);

        Context ctx(*this, dst);
        return std::forward<F>(f)(ctx);
    }

    void Reset();

  private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct alignas(16) FlowParam {
        uint32_t resolution[2uz] = {0u, 0u};
        uint32_t grid_size = 1u;
        float flow_scale = 1.0f;
        float shutter[2uz] = {0.0f, 0.0f};
        int32_t propagation_step = 0;
    };

    struct alignas(16) DrawParam {
        float texel[2uz] = {1.0f, 1.0f};
        float shutter[2uz] = {0.0f, 0.0f};
        float mix[2uz] = {0.0f, 1.0f};
        float falloff = 0.0f;
        int32_t sample_limit = 1;
    };

    struct Session {
        struct Input {
            NvOFBufferObj buf = nullptr;
            ComPtr<ID3D11RenderTargetView> rtv = nullptr;
        };

        struct Output {
            NvOFBufferObj buf = nullptr;
            ComPtr<ID3D11ShaderResourceView> srv = nullptr;
        };

        struct Resource {
            ComPtr<ID3D11Texture2D> tex = nullptr;
            ComPtr<ID3D11ShaderResourceView> srv = nullptr;
            ComPtr<ID3D11RenderTargetView> rtv = nullptr;
            ComPtr<ID3D11UnorderedAccessView> uav = nullptr;
        };

        NvOFObj ctx = nullptr;
        uint32_t grid_size = 1u;

        D3D11_VIEWPORT vp{};

        std::array<Input, 2uz> inputs{};
        std::array<Output, 2uz> outputs{};
        std::array<Output, 2uz> costs{};
        std::array<Resource, 3uz> resources{};
    };

    [[nodiscard]] std::error_code Acquire(ID3D11Texture2D* tex);

    [[nodiscard]] std::expected<Session, std::error_code> CreateSession(const ID& id) const;

    [[nodiscard]] std::expected<size_t, std::error_code> Regularize(const Session& session,
                                                                     const FlowParam& param) const;
    [[nodiscard]] std::expected<size_t, std::error_code> Propagate(
        const Session& session, size_t regularized, ID3D11ShaderResourceView* src,
        ID3D11ShaderResourceView* depth, const FlowParam& param) const;
    void ToABGR8(ID3D11RenderTargetView* dst, ID3D11ShaderResourceView* src, const D3D11_VIEWPORT& vp) const;

    [[nodiscard]] std::error_code Blur(ID3D11RenderTargetView* dst,
                                       const std::array<ID3D11ShaderResourceView*, 3uz>& src, DrawParam param,
                                       const D3D11_VIEWPORT& vp) const;

    void Release();

    std::mutex mutex_;

    ComPtr<ID3D11Device> device_ = nullptr;
    ComPtr<ID3D11DeviceContext> ctx_ = nullptr;

    ComPtr<ID3D11DepthStencilState> dss_ = nullptr;
    struct {
        ComPtr<ID3D11ComputeShader> regularize_flow = nullptr;
    } cs_{};
    ComPtr<ID3D11VertexShader> vs_ = nullptr;
    struct {
        ComPtr<ID3D11PixelShader> convert = nullptr;
        ComPtr<ID3D11PixelShader> decode_flow = nullptr;
        ComPtr<ID3D11PixelShader> propagate_init = nullptr;
        ComPtr<ID3D11PixelShader> propagate = nullptr;
        ComPtr<ID3D11PixelShader> blur = nullptr;
    } ps_{};
    ComPtr<ID3D11SamplerState> smp_ = nullptr;
    ComPtr<ID3D11Buffer> cb_ = nullptr;

    std::unordered_map<uint64_t, Session> sessions_;
};
}  // namespace blur::scene

template <>
struct std::is_error_code_enum<blur::scene::Error> : true_type {};
