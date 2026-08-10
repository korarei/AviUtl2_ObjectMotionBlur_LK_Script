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
#include <expected>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>

namespace blur::scene::direct3d {
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
  private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Texture {
        ComPtr<ID3D11Texture2D> tex = nullptr;
        ComPtr<ID3D11ShaderResourceView> srv = nullptr;
    };

  public:
    class Context;

    class Flow {
      public:
        uint32_t grid_size = 1u;

      private:
        struct Input {
            NvOFBufferObj buf = nullptr;
            ComPtr<ID3D11Texture2D> tex = nullptr;
            int section = -1;
            std::optional<int> frame = std::nullopt;
        };

        struct Output {
            Texture decoded{};
            Texture regularized{};
            Texture classified{};
        };

        struct Session {
            NvOFObj ctx = nullptr;
            uint32_t w = 0u;
            uint32_t h = 0u;
            uint32_t grid_size = 1u;
            std::array<Input, 2uz> inputs{};
        };

        friend class Renderer;
        friend class Context;

        uint32_t w = 0u, h = 0u;
        Texture src_{};
        Output output_{};
    };

    struct Param {
        int64_t id = 0;
        int section = -1;
        int frame = 0;
        NV_OF_PERF_LEVEL preset = NV_OF_PERF_LEVEL_SLOW;
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

        [[nodiscard]] std::expected<Flow, std::error_code> ComputeFlow(ID3D11Texture2D* src, ID3D11Texture2D* depth,
                                                                       const Param& param) const;
        [[nodiscard]] std::error_code Draw(const Flow& flow, const Param& param) const;
        [[nodiscard]] std::error_code Debug(const Flow& flow, const Param& param) const;

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
    struct alignas(16) FlowParam {
        uint32_t resolution[2uz] = {0u, 0u};
        uint32_t grid_size = 1u;
        float flow_scale = 1.0f;
    };

    struct alignas(16) LayerPropagationParam {
        float resolution[2uz] = {0.0f, 0.0f};
        float shutter[2uz] = {0.0f, 0.0f};
        int32_t step = 1;
        int32_t padding[3uz] = {0, 0, 0};
    };

    struct alignas(16) DrawParam {
        float texel[2uz] = {1.0f, 1.0f};
        float shutter[2uz] = {0.0f, 0.0f};
        float mix[2uz] = {0.0f, 1.0f};
        float falloff = 0.0f;
        int32_t sample_limit = 1;
    };

    struct alignas(16) DebugParam {
        float texel[2uz] = {1.0f, 1.0f};
        int32_t mode = 0;
        float scale = 1.0f;
    };

    [[nodiscard]] std::error_code Acquire(ID3D11Texture2D* tex);
    [[nodiscard]] std::expected<Flow::Session, std::error_code> CreateFlowSession(uint32_t w, uint32_t h,
                                                                                  NV_OF_PERF_LEVEL level) const;
    [[nodiscard]] std::expected<Flow::Output, std::error_code> CreateFlowOutput(
        const std::array<ID3D11ShaderResourceView*, 4uz>& flows, ID3D11ShaderResourceView* src,
        ID3D11ShaderResourceView* depth, const FlowParam& param) const;
    [[nodiscard]] std::expected<std::array<Texture, 2uz>, std::error_code> CreateLayerTiles(const Flow& flow,
                                                                                            const Param& param) const;
    [[nodiscard]] std::error_code ToABGR8(ID3D11RenderTargetView* dst, ID3D11ShaderResourceView* src,
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
        ComPtr<ID3D11PixelShader> classify_layer = nullptr;
        ComPtr<ID3D11PixelShader> reduce_layer = nullptr;
        ComPtr<ID3D11PixelShader> propagate_layer = nullptr;
        ComPtr<ID3D11PixelShader> blur = nullptr;
        ComPtr<ID3D11PixelShader> debug = nullptr;
    } ps_{};
    ComPtr<ID3D11SamplerState> smp_ = nullptr;
    ComPtr<ID3D11Buffer> cb_ = nullptr;

    std::unordered_map<int64_t, Flow::Session> sessions_;
};
}  // namespace blur::scene::direct3d

template <>
struct std::is_error_code_enum<blur::scene::direct3d::Error> : true_type {};
