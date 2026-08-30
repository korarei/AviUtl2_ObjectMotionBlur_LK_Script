#include "render.hpp"

#include <wrl/client.h>

#ifdef NOMINMAX
#undef NOMINMAX
#endif
#include <NvOFD3D11.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <bit>
#include <cstring>
#include <expected>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <blur.h>
#include <convert.h>
#include <debug.h>
#include <decode.h>
#include <fullscreen.h>
#include <premultiply.h>
#include <propagate.h>
#include <pull.h>
#include <push.h>
#include <resolve.h>
#include <smooth.h>

#include <intern/error/error.hpp>

namespace {
using Microsoft::WRL::ComPtr;

constexpr float kEpsilon = 1.0e-5f;

struct Param {
    struct alignas(16) Decode {
        uint32_t resolution[2uz] = {0u, 0u};
        uint32_t grid_size = 1u;
        float scale = 1.0f;
    };

    struct alignas(16) Premultiply {
        uint32_t resolution[2uz] = {0u, 0u};
        int32_t padding[2uz] = {0, 0};
    };

    struct alignas(16) Push {
        uint32_t resolution[4uz] = {0u, 0u, 0u, 0u};
    };

    struct alignas(16) Pull {
        uint32_t resolution[2uz] = {0u, 0u};
        int32_t padding[2uz] = {0, 0};
    };

    struct alignas(16) Resolve {
        uint32_t resolution[2uz] = {0u, 0u};
        int32_t padding[2uz] = {0, 0};
    };

    struct alignas(16) Smooth {
        uint32_t resolution[2uz] = {0u, 0u};
        uint32_t padding[2uz] = {0u, 0u};
    };

    struct alignas(16) Propagate {
        uint32_t resolution[2uz] = {0u, 0u};
        int32_t stride = 0;
        float padding = 0.0f;
    };

    struct alignas(16) Blur {
        float texel[2uz] = {1.0f, 1.0f};
        float mix[2uz] = {0.0f, 1.0f};
        float falloff[2uz] = {0.0f, 0.0f};
        int32_t sample_limit = 1;
        float padding = 0.0f;
    };

    struct alignas(16) Debug {
        float scale = 1.0f;
        float padding[3uz] = {0.0f, 0.0f, 0.0f};
    };
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

    struct Pyramid {
        struct Level {
            uint32_t w = 0u, h = 0u;
            Resource pushed{}, pulled{};
        };

        std::vector<Level> levels;
    };

    NvOFObj ctx = nullptr;
    uint32_t grid_size = 1u;

    D3D11_VIEWPORT vp{};

    std::array<Input, 2uz> inputs{};
    std::array<Output, 2uz> outputs{};
    std::array<Output, 2uz> costs{};
    std::array<Resource, 4uz> resources{};
    Pyramid pyramid{};
};

namespace d3d {
using blur::error::direct3d::Error;
using blur::error::direct3d::make_error_code;
using blur::error::direct3d::ToErrorCode;

std::mutex mutex;

ComPtr<ID3D11Device> device = nullptr;
ComPtr<ID3D11DeviceContext> ctx = nullptr;

ComPtr<ID3D11DepthStencilState> dss = nullptr;
struct {
    ComPtr<ID3D11ComputeShader> smooth = nullptr;
} cs{};
ComPtr<ID3D11VertexShader> vs = nullptr;
struct {
    ComPtr<ID3D11PixelShader> convert = nullptr;
    ComPtr<ID3D11PixelShader> decode = nullptr;
    ComPtr<ID3D11PixelShader> debug = nullptr;
    ComPtr<ID3D11PixelShader> premultiply = nullptr;
    ComPtr<ID3D11PixelShader> push = nullptr;
    ComPtr<ID3D11PixelShader> pull = nullptr;
    ComPtr<ID3D11PixelShader> resolve = nullptr;
    ComPtr<ID3D11PixelShader> propagate = nullptr;
    ComPtr<ID3D11PixelShader> blur = nullptr;
} ps{};
ComPtr<ID3D11SamplerState> smp = nullptr;
ComPtr<ID3D11Buffer> cb = nullptr;

std::unordered_map<uint64_t, Session> sessions;
std::unordered_map<uint64_t, uint64_t> references;
}  // namespace d3d

void Release() {
    std::unordered_map<uint64_t, Session>{}.swap(d3d::sessions);

    d3d::cb.Reset();
    d3d::smp.Reset();
    d3d::ps.blur.Reset();
    d3d::ps.propagate.Reset();
    d3d::ps.resolve.Reset();
    d3d::ps.pull.Reset();
    d3d::ps.push.Reset();
    d3d::ps.premultiply.Reset();
    d3d::ps.debug.Reset();
    d3d::ps.decode.Reset();
    d3d::ps.convert.Reset();
    d3d::vs.Reset();
    d3d::cs.smooth.Reset();
    d3d::dss.Reset();

    d3d::ctx.Reset();
    d3d::device.Reset();
}

[[nodiscard]] std::error_code EnsurePipeline(ID3D11Texture2D* tex) {
    {
        ComPtr<ID3D11Device> device;
        tex->GetDevice(&device);

        if (d3d::device != nullptr && d3d::device == device) {
            return {};
        }

        Release();

        d3d::device = std::move(device);
        d3d::device->GetImmediateContext(&d3d::ctx);
    }

#define TRY(expr, fallback)                                  \
    do {                                                     \
        if (const HRESULT hr_ = (expr); FAILED(hr_)) {       \
            Release();                                       \
            return d3d::ToErrorCode(hr_).value_or(fallback); \
        }                                                    \
    } while (false)

    {
        constexpr D3D11_DEPTH_STENCIL_DESC desc{
            .DepthEnable = FALSE,
            .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
            .DepthFunc = D3D11_COMPARISON_ALWAYS,
            .StencilEnable = FALSE,
            .StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK,
            .StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK,
            .FrontFace = {},
            .BackFace = {},
        };

        TRY(d3d::device->CreateDepthStencilState(&desc, &d3d::dss), d3d::Error::kCreateDepthStencilStateFailed);
    }

    TRY(d3d::device->CreateVertexShader(g_fullscreen, sizeof(g_fullscreen), nullptr, &d3d::vs),
        d3d::Error::kCreateVertexShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_convert, sizeof(g_convert), nullptr, &d3d::ps.convert),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_decode, sizeof(g_decode), nullptr, &d3d::ps.decode),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_debug, sizeof(g_debug), nullptr, &d3d::ps.debug),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_premultiply, sizeof(g_premultiply), nullptr, &d3d::ps.premultiply),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_push, sizeof(g_push), nullptr, &d3d::ps.push),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_pull, sizeof(g_pull), nullptr, &d3d::ps.pull),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_resolve, sizeof(g_resolve), nullptr, &d3d::ps.resolve),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreateComputeShader(g_smooth, sizeof(g_smooth), nullptr, &d3d::cs.smooth),
        d3d::Error::kCreateComputeShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_propagate, sizeof(g_propagate), nullptr, &d3d::ps.propagate),
        d3d::Error::kCreatePixelShaderFailed);

    TRY(d3d::device->CreatePixelShader(g_blur, sizeof(g_blur), nullptr, &d3d::ps.blur),
        d3d::Error::kCreatePixelShaderFailed);

    {
        constexpr D3D11_SAMPLER_DESC desc{
            .Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
            .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
            .MipLODBias = 0.0f,
            .MaxAnisotropy = 1u,
            .ComparisonFunc = D3D11_COMPARISON_NEVER,
            .BorderColor = {0.0f, 0.0f, 0.0f, 0.0f},
            .MinLOD = 0.0f,
            .MaxLOD = D3D11_FLOAT32_MAX,
        };

        TRY(d3d::device->CreateSamplerState(&desc, &d3d::smp), d3d::Error::kCreateSamplerStateFailed);
    }

    {
        constexpr size_t size = std::max({
            sizeof(Param::Decode),
            sizeof(Param::Premultiply),
            sizeof(Param::Push),
            sizeof(Param::Pull),
            sizeof(Param::Resolve),
            sizeof(Param::Smooth),
            sizeof(Param::Propagate),
            sizeof(Param::Blur),
            sizeof(Param::Debug),
        });

        constexpr D3D11_BUFFER_DESC desc{
            .ByteWidth = static_cast<uint32_t>(size),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = 0u,
            .StructureByteStride = 0u,
        };

        TRY(d3d::device->CreateBuffer(&desc, nullptr, &d3d::cb), d3d::Error::kCreateBufferFailed);
    }

#undef TRY

    return {};
}

template <typename T>
[[nodiscard]] std::error_code UploadConstantBuffer(const T& value) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = d3d::ctx->Map(d3d::cb.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
    if (FAILED(result)) {
        return d3d::ToErrorCode(result).value_or(d3d::Error::kMapBufferFailed);
    }

    std::memcpy(mapped.pData, &value, sizeof(value));
    d3d::ctx->Unmap(d3d::cb.Get(), 0u);
    return {};
}

void ToABGR8(ID3D11RenderTargetView* dst, ID3D11ShaderResourceView* src, const D3D11_VIEWPORT& vp) {
    static constexpr ID3D11ShaderResourceView* null_srv = nullptr;

    d3d::ctx->PSSetShader(d3d::ps.convert.Get(), nullptr, 0u);
    d3d::ctx->PSSetShaderResources(0u, 1u, &src);

    d3d::ctx->OMSetRenderTargets(1u, &dst, nullptr);

    d3d::ctx->RSSetViewports(1u, &vp);

    d3d::ctx->Draw(3u, 0u);

    d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    d3d::ctx->PSSetShaderResources(0u, 1u, &null_srv);
}
}  // namespace

namespace blur::scene::renderer {
namespace {
class ErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override { return "blur.scene.renderer"; }

    [[nodiscard]] std::string message(int condition) const override {
        switch (static_cast<Error>(condition)) {
            case Error::kOpticalFlowUnavailable:
                return "NVIDIA Optical Flow is not available";
            case Error::kInvalidOpticalFlowCall:
                return "NVIDIA Optical Flow API was called in an invalid order";
            case Error::kFlowAlreadyConsumed:
                return "Optical flow has already been consumed";
            case Error::kIncompatibleOpticalFlowVersion:
                return "NVIDIA Optical Flow API version is incompatible";
            case Error::kOpticalFlowNotInitialized:
                return "NVIDIA Optical Flow is not initialized";
            case Error::kOpticalFlowInternalError:
                return "NVIDIA Optical Flow reported an internal error";
            case Error::kOpticalFlowDirect3DFailed:
                return "Direct3D operation in NVIDIA Optical Flow failed";
            case Error::kUnknownOpticalFlowException:
                return "Unknown NVIDIA Optical Flow exception";
            case Error::kNoSupportedOutputGridSize:
                return "No supported NVIDIA Optical Flow output grid size was found";
            case Error::kRegisterOpticalFlowInputTextureFailed:
                return "Failed to register NVIDIA Optical Flow input texture";
        }

        return "Unknown NVIDIA Optical Flow error";
    }
};

[[nodiscard]] std::error_code ToErrorCode(NV_OF_STATUS status) noexcept {
    switch (status) {
        case NV_OF_SUCCESS:
            return {};
        case NV_OF_ERR_INVALID_PTR:
        case NV_OF_ERR_INVALID_PARAM:
            return std::make_error_code(std::errc::invalid_argument);
        case NV_OF_ERR_OUT_OF_MEMORY:
            return std::make_error_code(std::errc::not_enough_memory);
        case NV_OF_ERR_DEVICE_DOES_NOT_EXIST:
            return std::make_error_code(std::errc::no_such_device);
        case NV_OF_ERR_UNSUPPORTED_DEVICE:
        case NV_OF_ERR_UNSUPPORTED_FEATURE:
            return std::make_error_code(std::errc::not_supported);
        case NV_OF_ERR_OF_NOT_AVAILABLE:
            return Error::kOpticalFlowUnavailable;
        case NV_OF_ERR_INVALID_CALL:
            return Error::kInvalidOpticalFlowCall;
        case NV_OF_ERR_INVALID_VERSION:
            return Error::kIncompatibleOpticalFlowVersion;
        case NV_OF_ERR_NOT_INITIALIZED:
            return Error::kOpticalFlowNotInitialized;
        case NV_OF_ERR_GENERIC:
            return Error::kOpticalFlowInternalError;
    }
    return Error::kOpticalFlowInternalError;
}

constinit const ErrorCategory kErrorCategory{};

[[nodiscard]] std::expected<Session, std::error_code> CreateSession(const ID& id) {
    Session session;

    const auto preset = id.preset == 0   ? NV_OF_PERF_LEVEL_SLOW
                        : id.preset == 1 ? NV_OF_PERF_LEVEL_MEDIUM
                                         : NV_OF_PERF_LEVEL_FAST;

    const uint32_t w = static_cast<uint32_t>(id.w), h = static_cast<uint32_t>(id.h);

    session.ctx = NvOFD3D11::Create(d3d::device.Get(), d3d::ctx.Get(), w, h, NV_OF_BUFFER_FORMAT_ABGR8,
                                    NV_OF_MODE_OPTICALFLOW, preset);

    if (session.ctx == nullptr) {
        return std::unexpected(make_error_code(Error::kOpticalFlowUnavailable));
    }

    {
        uint32_t size = 1u;

        if (!session.ctx->CheckGridSize(1u) && !session.ctx->GetNextMinGridSize(1u, size)) {
            return std::unexpected(make_error_code(Error::kNoSupportedOutputGridSize));
        }

        session.ctx->Init(size, NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED, false, false, true, NV_OF_PRED_DIRECTION_BOTH);
        session.grid_size = size;
    }

    HRESULT result;

    {
        auto bufs = session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_INPUT, static_cast<uint32_t>(session.inputs.size()));
        if (bufs.size() != session.inputs.size()) {
            return std::unexpected(d3d::make_error_code(d3d::Error::kCreateTexture2DFailed));
        }

        for (size_t i = 0uz; i < session.inputs.size(); ++i) {
            auto& input = session.inputs[i];
            input.buf = std::move(bufs[i]);
            if (input.buf == nullptr) {
                return std::unexpected(d3d::make_error_code(d3d::Error::kCreateTexture2DFailed));
            }

            auto* const buf = static_cast<NvOFBufferD3D11*>(input.buf.get());
            result = d3d::device->CreateRenderTargetView(buf->getD3D11TextureHandle(), nullptr, &input.rtv);
            if (FAILED(result)) {
                return std::unexpected(d3d::ToErrorCode(result).value_or(d3d::Error::kCreateRenderTargetViewFailed));
            }
        }
    }

    {
        auto bufs =
            session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_OUTPUT, static_cast<uint32_t>(session.outputs.size()));
        if (bufs.size() != session.outputs.size()) {
            return std::unexpected(d3d::make_error_code(d3d::Error::kCreateTexture2DFailed));
        }

        for (size_t i = 0uz; i < session.outputs.size(); ++i) {
            auto& output = session.outputs[i];
            output.buf = std::move(bufs[i]);
            if (output.buf == nullptr) {
                return std::unexpected(d3d::make_error_code(d3d::Error::kCreateTexture2DFailed));
            }

            auto* const buf = static_cast<NvOFBufferD3D11*>(output.buf.get());
            result = d3d::device->CreateShaderResourceView(buf->getD3D11TextureHandle(), nullptr, &output.srv);
            if (FAILED(result)) {
                return std::unexpected(d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed));
            }
        }
    }

    {
        auto bufs = session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_COST, static_cast<uint32_t>(session.costs.size()));
        if (bufs.size() != session.costs.size()) {
            return std::unexpected(d3d::make_error_code(d3d::Error::kCreateTexture2DFailed));
        }

        for (size_t i = 0uz; i < session.costs.size(); ++i) {
            auto& cost = session.costs[i];
            cost.buf = std::move(bufs[i]);
            if (cost.buf == nullptr) {
                return std::unexpected(d3d::make_error_code(d3d::Error::kCreateTexture2DFailed));
            }

            auto* const buf = static_cast<NvOFBufferD3D11*>(cost.buf.get());
            result = d3d::device->CreateShaderResourceView(buf->getD3D11TextureHandle(), nullptr, &cost.srv);
            if (FAILED(result)) {
                return std::unexpected(d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed));
            }
        }
    }

    {
        const auto create_resource = [&](Session::Resource& resource, uint32_t width,
                                         uint32_t height) -> std::error_code {
            const D3D11_TEXTURE2D_DESC desc{
                .Width = width,
                .Height = height,
                .MipLevels = 1u,
                .ArraySize = 1u,
                .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                .SampleDesc = {.Count = 1u, .Quality = 0u},
                .Usage = D3D11_USAGE_DEFAULT,
                .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                .CPUAccessFlags = 0u,
                .MiscFlags = 0u,
            };

            result = d3d::device->CreateTexture2D(&desc, nullptr, &resource.tex);
            if (FAILED(result)) {
                return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateTexture2DFailed);
            }

            result = d3d::device->CreateShaderResourceView(resource.tex.Get(), nullptr, &resource.srv);
            if (FAILED(result)) {
                return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed);
            }

            result = d3d::device->CreateRenderTargetView(resource.tex.Get(), nullptr, &resource.rtv);
            if (FAILED(result)) {
                return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateRenderTargetViewFailed);
            }

            result = d3d::device->CreateUnorderedAccessView(resource.tex.Get(), nullptr, &resource.uav);
            if (FAILED(result)) {
                return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateUnorderedAccessViewFailed);
            }

            return {};
        };

        for (auto& resource : session.resources) {
            if (const auto ec = create_resource(resource, w, h); ec != std::error_code{}) {
                return std::unexpected(ec);
            }
        }

        uint32_t level_w = w, level_h = h;

        while (true) {
            auto& level = session.pyramid.levels.emplace_back(level_w, level_h);

            if (const auto ec = create_resource(level.pushed, level_w, level_h); ec != std::error_code{}) {
                return std::unexpected(ec);
            }

            const bool is_coarsest = level_w == 1u && level_h == 1u;

            if (session.pyramid.levels.size() > 1uz && !is_coarsest) {
                if (const auto ec = create_resource(level.pulled, level_w, level_h); ec != std::error_code{}) {
                    return std::unexpected(ec);
                }
            }

            if (is_coarsest) {
                break;
            }

            level_w = std::max((level_w + 1u) / 2u, 1u);
            level_h = std::max((level_h + 1u) / 2u, 1u);
        }
    }

    session.vp = {
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = static_cast<float>(w),
        .Height = static_cast<float>(h),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    return session;
}

// オプティカルフローを正則化し、2つの代表フローの初期値を書き込む:
// - session.resources[0uz]: 第一代表フロー
// - session.resources[2uz]: 第二代表フロー
[[nodiscard]] std::error_code Regularize(const Session& session, ID3D11ShaderResourceView* depth,
                                         const Parameter& param) {
    static constexpr ID3D11ShaderResourceView* null_srvs[4uz] = {nullptr, nullptr, nullptr, nullptr};
    static constexpr ID3D11UnorderedAccessView* null_uavs[2uz] = {nullptr, nullptr};

    const uint32_t w = static_cast<uint32_t>(param.id.w), h = static_cast<uint32_t>(param.id.h);

    d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    d3d::ctx->PSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());
    d3d::ctx->CSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    d3d::ctx->CSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());

    {
        const Param::Decode shader_param{
            .resolution = {w, h},
            .grid_size = session.grid_size,
            .scale = param.scale,
        };

        ID3D11ShaderResourceView* const inputs[] = {
            session.outputs[0uz].srv.Get(),
            session.outputs[1uz].srv.Get(),
            session.costs[0uz].srv.Get(),
            session.costs[1uz].srv.Get(),
        };

        if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
            return ec;
        }

        d3d::ctx->PSSetShader(d3d::ps.decode.Get(), nullptr, 0u);
        d3d::ctx->PSSetShaderResources(0u, 4u, inputs);

        d3d::ctx->OMSetRenderTargets(1u, session.resources[0uz].rtv.GetAddressOf(), nullptr);

        d3d::ctx->RSSetViewports(1u, &session.vp);

        d3d::ctx->Draw(3u, 0u);

        d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
        d3d::ctx->PSSetShaderResources(0u, 4u, null_srvs);
    }

    {
        const auto& levels = session.pyramid.levels;
        const auto& base = levels.front();

        {
            const Param::Premultiply shader_param{
                .resolution = {w, h},
            };

            ID3D11ShaderResourceView* const inputs[] = {
                session.resources[0uz].srv.Get(),
                depth,
            };

            if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
                return ec;
            }

            d3d::ctx->PSSetShader(d3d::ps.premultiply.Get(), nullptr, 0u);
            d3d::ctx->PSSetShaderResources(0u, 2u, inputs);

            d3d::ctx->OMSetRenderTargets(1u, base.pushed.rtv.GetAddressOf(), nullptr);

            // d3d::ctx->RSSetViewports(1u, &session.vp);

            d3d::ctx->Draw(3u, 0u);

            d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
            d3d::ctx->PSSetShaderResources(0u, 2u, null_srvs);
        }

        d3d::ctx->PSSetShader(d3d::ps.push.Get(), nullptr, 0u);

        for (size_t i = 1uz; i < levels.size(); ++i) {
            const auto& src = levels[i - 1uz];
            const auto& dst = levels[i];
            const Param::Push shader_param{
                .resolution = {dst.w, dst.h, src.w, src.h},
            };

            ID3D11ShaderResourceView* const inputs[] = {
                src.pushed.srv.Get(),
                depth,
            };

            if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
                return ec;
            }

            const D3D11_VIEWPORT vp{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = static_cast<float>(dst.w),
                .Height = static_cast<float>(dst.h),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f,
            };

            d3d::ctx->PSSetShaderResources(0u, 2u, inputs);

            d3d::ctx->OMSetRenderTargets(1u, dst.pushed.rtv.GetAddressOf(), nullptr);

            d3d::ctx->RSSetViewports(1u, &vp);

            d3d::ctx->Draw(3u, 0u);

            d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
            d3d::ctx->PSSetShaderResources(0u, 2u, null_srvs);
        }

        ID3D11ShaderResourceView* coarse = levels.back().pushed.srv.Get();

        d3d::ctx->PSSetShader(d3d::ps.pull.Get(), nullptr, 0u);

        for (size_t i = levels.size() - 1uz; i > 1uz;) {
            --i;
            const auto& level = levels[i];
            const Param::Pull shader_param{
                .resolution = {level.w, level.h},
            };

            ID3D11ShaderResourceView* const inputs[] = {
                level.pushed.srv.Get(),
                coarse,
                depth,
            };

            if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
                return ec;
            }

            const D3D11_VIEWPORT vp{
                .TopLeftX = 0.0f,
                .TopLeftY = 0.0f,
                .Width = static_cast<float>(level.w),
                .Height = static_cast<float>(level.h),
                .MinDepth = 0.0f,
                .MaxDepth = 1.0f,
            };

            d3d::ctx->PSSetShaderResources(0u, 3u, inputs);

            d3d::ctx->OMSetRenderTargets(1u, level.pulled.rtv.GetAddressOf(), nullptr);

            d3d::ctx->RSSetViewports(1u, &vp);

            d3d::ctx->Draw(3u, 0u);

            d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
            d3d::ctx->PSSetShaderResources(0u, 3u, null_srvs);

            coarse = level.pulled.srv.Get();
        }

        const Param::Resolve shader_param{
            .resolution = {w, h},
        };

        coarse = levels.size() > 1uz ? coarse : base.pushed.srv.Get();

        ID3D11ShaderResourceView* const inputs[] = {
            base.pushed.srv.Get(),
            coarse,
            depth,
        };

        if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
            return ec;
        }

        d3d::ctx->PSSetShader(d3d::ps.resolve.Get(), nullptr, 0u);
        d3d::ctx->PSSetShaderResources(0u, 3u, inputs);

        d3d::ctx->OMSetRenderTargets(1u, session.resources[1uz].rtv.GetAddressOf(), nullptr);

        d3d::ctx->RSSetViewports(1u, &session.vp);

        d3d::ctx->Draw(3u, 0u);

        d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
        d3d::ctx->PSSetShaderResources(0u, 3u, null_srvs);
    }

    {
        d3d::ctx->CSSetShader(d3d::cs.smooth.Get(), nullptr, 0u);

        const Param::Smooth shader_param{
            .resolution = {w, h},
        };

        if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
            return ec;
        }

        ID3D11ShaderResourceView* const inputs[] = {
            session.resources[1uz].srv.Get(),
            depth,
        };

        ID3D11UnorderedAccessView* const outputs[] = {
            session.resources[0uz].uav.Get(),
            session.resources[2uz].uav.Get(),
        };

        d3d::ctx->CSSetShaderResources(0u, 2u, inputs);
        d3d::ctx->CSSetUnorderedAccessViews(0u, 2u, outputs, nullptr);

        d3d::ctx->Dispatch((w + 15u) / 16u, (h + 15u) / 16u, 1u);

        d3d::ctx->CSSetUnorderedAccessViews(0u, 2u, null_uavs, nullptr);
        d3d::ctx->CSSetShaderResources(0u, 2u, null_srvs);
    }

    return {};
}

// Jump Flooding Algorithm を用いて 2 層のフロー場を伝搬:
// - 入力: session.resources[0uz] (第1フロー) & session.resources[2uz] (第2フロー)
// - 戻り値: 有効なバッファペアの基準インデックス `curr`:
//     session.resources[curr]: 第1フロー
//     session.resources[curr + 2uz]: 第2フロー
[[nodiscard]] std::expected<size_t, std::error_code> Propagate(const Session& session, ID3D11ShaderResourceView* depth,
                                                               const Parameter& param) {
    static_assert(std::tuple_size_v<decltype(Session::resources)> == 4uz);

    static constexpr ID3D11ShaderResourceView* null_srvs[3uz] = {nullptr, nullptr, nullptr};

    const uint32_t w = static_cast<uint32_t>(param.id.w), h = static_cast<uint32_t>(param.id.h);

    size_t curr = 0uz;
    size_t next = curr ^ 1uz;

    d3d::ctx->RSSetViewports(1u, &session.vp);
    d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    d3d::ctx->PSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());
    d3d::ctx->PSSetShader(d3d::ps.propagate.Get(), nullptr, 0u);

    Param::Propagate shader_param{
        .resolution = {w, h},
        .stride = 0,
    };

    for (uint32_t step = std::bit_floor(std::max(w, h)); step > 0u; step >>= 1u) {
        shader_param.stride = static_cast<int32_t>(step);

        if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
            return std::unexpected(ec);
        }

        ID3D11ShaderResourceView* const inputs[3uz] = {
            session.resources[curr].srv.Get(),
            session.resources[curr + 2uz].srv.Get(),
            depth,
        };

        ID3D11RenderTargetView* const outputs[2uz] = {
            session.resources[next].rtv.Get(),
            session.resources[next + 2uz].rtv.Get(),
        };

        d3d::ctx->PSSetShaderResources(0u, 3u, inputs);

        d3d::ctx->OMSetRenderTargets(2u, outputs, nullptr);

        d3d::ctx->Draw(3u, 0u);

        d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
        d3d::ctx->PSSetShaderResources(0u, 3u, null_srvs);

        std::swap(curr, next);
    }

    return curr;
}

[[nodiscard]] std::error_code Blur(ID3D11RenderTargetView* dst, const std::array<ID3D11ShaderResourceView*, 3uz>& src,
                                   const Parameter& param, const D3D11_VIEWPORT& vp) {
    static constexpr ID3D11ShaderResourceView* null_srvs[3uz] = {nullptr, nullptr, nullptr};

    const float edge = std::max(param.falloff_amount, kEpsilon);

    const Param::Blur shader_param{
        .texel =
            {
                1.0f / static_cast<float>(param.id.w),
                1.0f / static_cast<float>(param.id.h),
            },
        .mix =
            {
                1.0f - param.mix,
                param.mix,
            },
        .falloff =
            {
                param.falloff_edge == 0 ? kEpsilon : edge,
                param.falloff_edge == 1 ? kEpsilon : edge,
            },
        .sample_limit = param.sample_limit,
    };

    if (const auto ec = UploadConstantBuffer(shader_param); ec != std::error_code{}) {
        return ec;
    }

    d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    d3d::ctx->PSSetShader(d3d::ps.blur.Get(), nullptr, 0u);
    d3d::ctx->PSSetShaderResources(0u, static_cast<UINT>(src.size()), src.data());
    d3d::ctx->PSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());

    d3d::ctx->OMSetRenderTargets(1u, &dst, nullptr);

    d3d::ctx->RSSetViewports(1u, &vp);

    d3d::ctx->Draw(3u, 0u);

    d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    d3d::ctx->PSSetShaderResources(0u, 3u, null_srvs);

    return {};
}

[[nodiscard]] std::error_code Debug(ID3D11RenderTargetView* dst, ID3D11ShaderResourceView* flow, float scale,
                                    const D3D11_VIEWPORT& vp) {
    static constexpr ID3D11ShaderResourceView* null_srv = nullptr;

    const Param::Debug param{
        .scale = scale,
    };

    if (const auto ec = UploadConstantBuffer(param); ec != std::error_code{}) {
        return ec;
    }

    d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    d3d::ctx->PSSetShader(d3d::ps.debug.Get(), nullptr, 0u);
    d3d::ctx->PSSetShaderResources(0u, 1u, &flow);

    d3d::ctx->OMSetRenderTargets(1u, &dst, nullptr);

    d3d::ctx->RSSetViewports(1u, &vp);

    d3d::ctx->Draw(3u, 0u);

    d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    d3d::ctx->PSSetShaderResources(0u, 1u, &null_srv);

    return {};
}
}  // namespace

std::error_code make_error_code(Error error) noexcept { return {static_cast<int>(error), kErrorCategory}; }

void Add(const ID& id) {
    const std::lock_guard lock(d3d::mutex);
    const auto key = std::bit_cast<uint64_t>(id);
    ++d3d::references[key];
}

void Remove(const ID& id) {
    const std::lock_guard lock(d3d::mutex);
    const auto key = std::bit_cast<uint64_t>(id);
    const auto it = d3d::references.find(key);
    if (it == d3d::references.end() || it->second == 0u) {
        return;
    }

    if (--it->second == 0u) {
        d3d::references.erase(it);
        d3d::sessions.erase(key);
    }
}

[[nodiscard]] Context CreateContext(ID3D11Texture2D* dst) { return Context(dst); }

std::error_code Context::Draw(const Sequence& sequence, const Parameter& param) const {
    struct {
        std::array<ComPtr<ID3D11ShaderResourceView>, 2uz> inputs{};
        ComPtr<ID3D11ShaderResourceView> depth = nullptr;
    } view{};

    const auto id = std::bit_cast<uint64_t>(param.id);

    HRESULT result;

    Session* session = nullptr;

    try {
        auto it = d3d::sessions.try_emplace(id).first;
        session = &it->second;

        if (session->ctx == nullptr) {
            auto new_session = CreateSession(param.id);
            if (new_session.has_value()) {
                *session = std::move(*new_session);
            } else {
                d3d::sessions.erase(id);
                return new_session.error();
            }
        }

        D3D11_TEXTURE2D_DESC desc{};
        dst_->GetDesc(&desc);

        if (desc.Width != static_cast<UINT>(param.id.w) || desc.Height != static_cast<UINT>(param.id.h)) {
            d3d::sessions.erase(id);
            return std::make_error_code(std::errc::invalid_argument);
        }

        for (size_t i = 0uz; i < view.inputs.size(); ++i) {
            result = d3d::device->CreateShaderResourceView(sequence.inputs[i], nullptr, &view.inputs[i]);
            if (FAILED(result)) {
                d3d::sessions.erase(id);
                return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed);
            }
        }

        result = d3d::device->CreateShaderResourceView(sequence.depth, nullptr, &view.depth);
        if (FAILED(result)) {
            d3d::sessions.erase(id);
            return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateShaderResourceViewFailed);
        }

        for (size_t i = 0uz; i < 2uz; ++i) {
            ToABGR8(session->inputs[i].rtv.Get(), view.inputs[i].Get(), session->vp);
        }

        // なぜか2回実行すると精度上がる (Temporal Hint が効いてる？)

        session->ctx->Execute(session->inputs[0uz].buf.get(), session->inputs[1uz].buf.get(),
                              session->outputs[0uz].buf.get(), nullptr, session->costs[0uz].buf.get(), 0u, nullptr,
                              nullptr, 0u, nullptr, param.should_use_temporal_hints ? NV_OF_FALSE : NV_OF_TRUE,
                              session->outputs[1uz].buf.get(), session->costs[1uz].buf.get());

        session->ctx->Execute(session->inputs[0uz].buf.get(), session->inputs[1uz].buf.get(),
                              session->outputs[0uz].buf.get(), nullptr, session->costs[0uz].buf.get(), 0u, nullptr,
                              nullptr, 0u, nullptr, param.should_use_temporal_hints ? NV_OF_FALSE : NV_OF_TRUE,
                              session->outputs[1uz].buf.get(), session->costs[1uz].buf.get());
    } catch (const NvOFException& exception) {
        d3d::sessions.erase(id);
        return ToErrorCode(exception.getErrorCode());
    } catch (const std::bad_alloc&) {
        d3d::sessions.erase(id);
        return std::make_error_code(std::errc::not_enough_memory);
    } catch (const std::invalid_argument&) {
        d3d::sessions.erase(id);
        return std::make_error_code(std::errc::invalid_argument);
    } catch (const DXException&) {
        d3d::sessions.erase(id);
        return make_error_code(Error::kOpticalFlowDirect3DFailed);
    } catch (const std::exception&) {
        d3d::sessions.erase(id);
        return make_error_code(Error::kOpticalFlowInternalError);
    } catch (...) {
        d3d::sessions.erase(id);
        return make_error_code(Error::kUnknownOpticalFlowException);
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    result = d3d::device->CreateRenderTargetView(dst_, nullptr, &rtv);
    if (FAILED(result)) {
        return d3d::ToErrorCode(result).value_or(d3d::Error::kCreateRenderTargetViewFailed);
    }

    if (const auto ec = Regularize(*session, view.depth.Get(), param); ec != std::error_code{}) {
        return ec;
    }

    if (param.view_mode == 1) {
        return Debug(rtv.Get(), session->resources[0uz].srv.Get(), param.scale, session->vp);
    }

    const auto pos = Propagate(*session, view.depth.Get(), param);
    if (!pos.has_value()) {
        return pos.error();
    }

    if (param.view_mode == 2) {
        return Debug(rtv.Get(), session->resources[*pos].srv.Get(), param.scale, session->vp);
    }

    if (param.view_mode == 3) {
        return Debug(rtv.Get(), session->resources[*pos + 2uz].srv.Get(), param.scale, session->vp);
    }

    const std::array<ID3D11ShaderResourceView*, 3uz> inputs = {
        view.inputs[0uz].Get(),
        session->resources[*pos].srv.Get(),
        session->resources[*pos + 2uz].srv.Get(),
    };

    return Blur(rtv.Get(), inputs, param, session->vp);
}

std::error_code Render(ID3D11Texture2D* dst, FunctionRef callback) {
    const std::lock_guard lock(d3d::mutex);

    std::error_code ec;

    if (ec = EnsurePipeline(dst); ec != std::error_code{}) {
        return ec;
    }

    d3d::ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    d3d::ctx->OMSetDepthStencilState(d3d::dss.Get(), 0u);

    d3d::ctx->RSSetState(nullptr);

    d3d::ctx->GSSetShader(nullptr, nullptr, 0u);

    d3d::ctx->IASetInputLayout(nullptr);
    d3d::ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d::ctx->VSSetShader(d3d::vs.Get(), nullptr, 0u);

    if (ec = callback(CreateContext(dst)); ec != std::error_code{}) {
        Release();
        return ec;
    }

    return {};
}

void Reset() {
    const std::lock_guard lock(d3d::mutex);
    Release();
}

void Deinit() {
    const std::lock_guard lock(d3d::mutex);
    Release();
    std::unordered_map<uint64_t, uint64_t>{}.swap(d3d::references);
}
}  // namespace blur::scene::renderer
