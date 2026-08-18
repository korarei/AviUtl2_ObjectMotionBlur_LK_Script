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

#include <blur.h>
#include <convert.h>
#include <decode_flow.h>
#include <fullscreen.h>
#include <propagate.h>
#include <propagate_init.h>
#include <regularize_flow.h>

namespace {
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

namespace d3d {
std::mutex mutex;

ComPtr<ID3D11Device> device = nullptr;
ComPtr<ID3D11DeviceContext> ctx = nullptr;

ComPtr<ID3D11DepthStencilState> dss = nullptr;
struct {
    ComPtr<ID3D11ComputeShader> regularize_flow = nullptr;
} cs{};
ComPtr<ID3D11VertexShader> vs = nullptr;
struct {
    ComPtr<ID3D11PixelShader> convert = nullptr;
    ComPtr<ID3D11PixelShader> decode_flow = nullptr;
    ComPtr<ID3D11PixelShader> propagate_init = nullptr;
    ComPtr<ID3D11PixelShader> propagate = nullptr;
    ComPtr<ID3D11PixelShader> blur = nullptr;
} ps{};
ComPtr<ID3D11SamplerState> smp = nullptr;
ComPtr<ID3D11Buffer> cb = nullptr;

std::unordered_map<uint64_t, Session> sessions;

void Release() {
    sessions.clear();

    cb.Reset();
    smp.Reset();
    ps.blur.Reset();
    ps.propagate.Reset();
    ps.propagate_init.Reset();
    ps.decode_flow.Reset();
    ps.convert.Reset();
    vs.Reset();
    cs.regularize_flow.Reset();
    dss.Reset();

    ctx.Reset();
    device.Reset();
}
}  // namespace d3d
}  // namespace

namespace blur::scene::renderer {
namespace {
class ErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] const char* name() const noexcept override { return "blur.scene.renderer"; }

    [[nodiscard]] std::string message(int condition) const override {
        switch (static_cast<Error>(condition)) {
            case Error::kDeviceRemoved:
                return "Direct3D device was removed";
            case Error::kDeviceReset:
                return "Direct3D device was reset";
            case Error::kDeviceHung:
                return "Direct3D device stopped responding";
            case Error::kMapBufferFailed:
                return "Failed to map buffer";
            case Error::kCreateBufferFailed:
                return "Failed to create buffer";
            case Error::kCreateTexture2DFailed:
                return "Failed to create texture 2D";
            case Error::kCreateRenderTargetViewFailed:
                return "Failed to create render target view";
            case Error::kCreateShaderResourceViewFailed:
                return "Failed to create shader resource view";
            case Error::kCreateUnorderedAccessViewFailed:
                return "Failed to create unordered access view";
            case Error::kCreateVertexShaderFailed:
                return "Failed to create vertex shader";
            case Error::kCreatePixelShaderFailed:
                return "Failed to create pixel shader";
            case Error::kCreateComputeShaderFailed:
                return "Failed to create compute shader";
            case Error::kCreateDepthStencilStateFailed:
                return "Failed to create depth stencil state";
            case Error::kCreateSamplerStateFailed:
                return "Failed to create sampler state";
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

        return "Unknown Direct3D and NVIDIA Optical Flow error";
    }
};

[[nodiscard]] std::error_code ToErrorCode(HRESULT result, Error fallback) noexcept {
    switch (result) {
        case E_INVALIDARG:
            return std::make_error_code(std::errc::invalid_argument);
        case E_OUTOFMEMORY:
            return std::make_error_code(std::errc::not_enough_memory);
        case E_ACCESSDENIED:
            return std::make_error_code(std::errc::permission_denied);
        case E_NOTIMPL:
        case E_NOINTERFACE:
            return std::make_error_code(std::errc::not_supported);
        case E_ABORT:
            return std::make_error_code(std::errc::operation_canceled);
        case DXGI_ERROR_UNSUPPORTED:
            return std::make_error_code(std::errc::not_supported);
        case DXGI_ERROR_DEVICE_REMOVED:
            return Error::kDeviceRemoved;
        case DXGI_ERROR_DEVICE_RESET:
            return Error::kDeviceReset;
        case DXGI_ERROR_DEVICE_HUNG:
            return Error::kDeviceHung;
        default:
            return fallback;
    }
}

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

[[nodiscard]] std::error_code EnsureResources(ID3D11Texture2D* tex) {
    {
        ComPtr<ID3D11Device> device;
        tex->GetDevice(&device);

        if (d3d::device != nullptr && d3d::device == device) {
            return {};
        }

        d3d::Release();

        d3d::device = std::move(device);
        d3d::device->GetImmediateContext(&d3d::ctx);
    }

    HRESULT result;

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

        result = d3d::device->CreateDepthStencilState(&desc, &d3d::dss);
        if (FAILED(result)) {
            d3d::Release();
            return ToErrorCode(result, Error::kCreateDepthStencilStateFailed);
        }
    }

    result = d3d::device->CreateVertexShader(g_fullscreen, sizeof(g_fullscreen), nullptr, &d3d::vs);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreateVertexShaderFailed);
    }

    result = d3d::device->CreatePixelShader(g_convert, sizeof(g_convert), nullptr, &d3d::ps.convert);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = d3d::device->CreatePixelShader(g_decode_flow, sizeof(g_decode_flow), nullptr, &d3d::ps.decode_flow);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = d3d::device->CreateComputeShader(g_regularize_flow, sizeof(g_regularize_flow), nullptr,
                                              &d3d::cs.regularize_flow);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreateComputeShaderFailed);
    }

    result =
        d3d::device->CreatePixelShader(g_propagate_init, sizeof(g_propagate_init), nullptr, &d3d::ps.propagate_init);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = d3d::device->CreatePixelShader(g_propagate, sizeof(g_propagate), nullptr, &d3d::ps.propagate);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

    result = d3d::device->CreatePixelShader(g_blur, sizeof(g_blur), nullptr, &d3d::ps.blur);
    if (FAILED(result)) {
        d3d::Release();
        return ToErrorCode(result, Error::kCreatePixelShaderFailed);
    }

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

        result = d3d::device->CreateSamplerState(&desc, &d3d::smp);
        if (FAILED(result)) {
            d3d::Release();
            return ToErrorCode(result, Error::kCreateSamplerStateFailed);
        }
    }

    {
        constexpr size_t size = std::max({
            sizeof(FlowParam),
            sizeof(DrawParam),
        });

        constexpr D3D11_BUFFER_DESC desc{
            .ByteWidth = static_cast<uint32_t>(size),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags = 0u,
            .StructureByteStride = 0u,
        };

        result = d3d::device->CreateBuffer(&desc, nullptr, &d3d::cb);
        if (FAILED(result)) {
            d3d::Release();
            return ToErrorCode(result, Error::kCreateBufferFailed);
        }
    }

    return {};
}

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
            return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
        }

        for (size_t i = 0uz; i < session.inputs.size(); ++i) {
            auto& input = session.inputs[i];
            input.buf = std::move(bufs[i]);
            if (input.buf == nullptr) {
                return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
            }

            auto* const buf = static_cast<NvOFBufferD3D11*>(input.buf.get());
            result = d3d::device->CreateRenderTargetView(buf->getD3D11TextureHandle(), nullptr, &input.rtv);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kCreateRenderTargetViewFailed));
            }
        }
    }

    {
        auto bufs =
            session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_OUTPUT, static_cast<uint32_t>(session.outputs.size()));
        if (bufs.size() != session.outputs.size()) {
            return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
        }

        for (size_t i = 0uz; i < session.outputs.size(); ++i) {
            auto& output = session.outputs[i];
            output.buf = std::move(bufs[i]);
            if (output.buf == nullptr) {
                return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
            }

            auto* const buf = static_cast<NvOFBufferD3D11*>(output.buf.get());
            result = d3d::device->CreateShaderResourceView(buf->getD3D11TextureHandle(), nullptr, &output.srv);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kCreateShaderResourceViewFailed));
            }
        }
    }

    {
        auto bufs = session.ctx->CreateBuffers(NV_OF_BUFFER_USAGE_COST, static_cast<uint32_t>(session.costs.size()));
        if (bufs.size() != session.costs.size()) {
            return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
        }

        for (size_t i = 0uz; i < session.costs.size(); ++i) {
            auto& cost = session.costs[i];
            cost.buf = std::move(bufs[i]);
            if (cost.buf == nullptr) {
                return std::unexpected(make_error_code(Error::kCreateTexture2DFailed));
            }

            auto* const buf = static_cast<NvOFBufferD3D11*>(cost.buf.get());
            result = d3d::device->CreateShaderResourceView(buf->getD3D11TextureHandle(), nullptr, &cost.srv);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kCreateShaderResourceViewFailed));
            }
        }
    }

    {
        const D3D11_TEXTURE2D_DESC desc{
            .Width = w,
            .Height = h,
            .MipLevels = 1u,
            .ArraySize = 1u,
            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
            .SampleDesc = {.Count = 1u, .Quality = 0u},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
            .CPUAccessFlags = 0u,
            .MiscFlags = 0u,
        };

        for (auto& res : session.resources) {
            result = d3d::device->CreateTexture2D(&desc, nullptr, &res.tex);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kCreateTexture2DFailed));
            }

            result = d3d::device->CreateShaderResourceView(res.tex.Get(), nullptr, &res.srv);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kCreateShaderResourceViewFailed));
            }

            result = d3d::device->CreateRenderTargetView(res.tex.Get(), nullptr, &res.rtv);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kCreateRenderTargetViewFailed));
            }

            result = d3d::device->CreateUnorderedAccessView(res.tex.Get(), nullptr, &res.uav);
            if (FAILED(result)) {
                return std::unexpected(ToErrorCode(result, Error::kCreateUnorderedAccessViewFailed));
            }
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

[[nodiscard]] std::expected<size_t, std::error_code> Regularize(const Session& session, const FlowParam& param) {
    static constexpr ID3D11ShaderResourceView* null_ps_srvs[4uz] = {nullptr, nullptr, nullptr, nullptr};
    static constexpr ID3D11ShaderResourceView* null_cs_srvs[1uz] = {nullptr};
    static constexpr ID3D11UnorderedAccessView* null_uav = nullptr;

    const auto set_params = [](const FlowParam& value) -> std::error_code {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = d3d::ctx->Map(d3d::cb.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &value, sizeof(value));
        d3d::ctx->Unmap(d3d::cb.Get(), 0u);
        return {};
    };

    const uint32_t width = param.resolution[0uz];
    const uint32_t height = param.resolution[1uz];

    if (const auto ec = set_params(param); ec != std::error_code{}) {
        return std::unexpected(ec);
    }

    d3d::ctx->IASetInputLayout(nullptr);
    d3d::ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d::ctx->VSSetShader(d3d::vs.Get(), nullptr, 0u);
    d3d::ctx->RSSetViewports(1u, &session.vp);
    d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());

    {
        const std::array<ID3D11ShaderResourceView*, 4uz> inputs = {
            session.outputs[0uz].srv.Get(),
            session.outputs[1uz].srv.Get(),
            session.costs[0uz].srv.Get(),
            session.costs[1uz].srv.Get(),
        };
        ID3D11RenderTargetView* const target = session.resources[0uz].rtv.Get();

        d3d::ctx->PSSetShader(d3d::ps.decode_flow.Get(), nullptr, 0u);
        d3d::ctx->PSSetShaderResources(0u, static_cast<UINT>(inputs.size()), inputs.data());
        d3d::ctx->OMSetRenderTargets(1u, &target, nullptr);
        d3d::ctx->Draw(3u, 0u);
        d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
        d3d::ctx->PSSetShaderResources(0u, 4u, null_ps_srvs);
    }

    d3d::ctx->CSSetShader(d3d::cs.regularize_flow.Get(), nullptr, 0u);
    d3d::ctx->CSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());

    const std::array<int32_t, 7uz> steps = {0, 2, 4, 8, 16, 32, 64};
    size_t input_index = 0uz;
    size_t output_index = 1uz;

    for (const int32_t step : steps) {
        FlowParam regularize_param = param;
        regularize_param.propagation_step = step;

        if (const auto ec = set_params(regularize_param); ec != std::error_code{}) {
            return std::unexpected(ec);
        }

        ID3D11ShaderResourceView* const input = session.resources[input_index].srv.Get();
        ID3D11UnorderedAccessView* const target = session.resources[output_index].uav.Get();

        d3d::ctx->CSSetShaderResources(0u, 1u, &input);
        d3d::ctx->CSSetUnorderedAccessViews(0u, 1u, &target, nullptr);
        d3d::ctx->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
        d3d::ctx->CSSetUnorderedAccessViews(0u, 1u, &null_uav, nullptr);
        d3d::ctx->CSSetShaderResources(0u, 1u, null_cs_srvs);

        std::swap(input_index, output_index);
    }

    return input_index;
}

[[nodiscard]] std::expected<size_t, std::error_code> Propagate(const Session& session, size_t regularized,
                                                               ID3D11ShaderResourceView* src,
                                                               ID3D11ShaderResourceView* depth,
                                                               const FlowParam& param) {
    static constexpr ID3D11ShaderResourceView* null_srvs[4uz] = {nullptr, nullptr, nullptr, nullptr};

    if (regularized >= session.resources.size()) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    const auto set_params = [](const FlowParam& value) -> std::error_code {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = d3d::ctx->Map(d3d::cb.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &value, sizeof(value));
        d3d::ctx->Unmap(d3d::cb.Get(), 0u);
        return {};
    };

    const uint32_t width = param.resolution[0uz];
    const uint32_t height = param.resolution[1uz];
    const uint32_t range = std::max(width, height);
    size_t current = (regularized + 1uz) % session.resources.size();
    size_t next = (current + 1uz) % session.resources.size();

    d3d::ctx->IASetInputLayout(nullptr);
    d3d::ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d::ctx->VSSetShader(d3d::vs.Get(), nullptr, 0u);
    d3d::ctx->PSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());
    d3d::ctx->RSSetViewports(1u, &session.vp);
    d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());

    {
        FlowParam propagation_param = param;
        propagation_param.propagation_step = 0;

        if (const auto ec = set_params(propagation_param); ec != std::error_code{}) {
            return std::unexpected(ec);
        }

        const std::array<ID3D11ShaderResourceView*, 4uz> inputs = {
            session.resources[regularized].srv.Get(),
            src,
            depth,
            nullptr,
        };
        ID3D11RenderTargetView* const target = session.resources[current].rtv.Get();

        d3d::ctx->PSSetShader(d3d::ps.propagate_init.Get(), nullptr, 0u);
        d3d::ctx->PSSetShaderResources(0u, static_cast<UINT>(inputs.size()), inputs.data());
        d3d::ctx->OMSetRenderTargets(1u, &target, nullptr);
        d3d::ctx->Draw(3u, 0u);
        d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
        d3d::ctx->PSSetShaderResources(0u, 4u, null_srvs);
    }

    for (uint32_t step = 1u, distance = 0u; distance < range; step <<= 1u) {
        FlowParam propagation_param = param;
        propagation_param.propagation_step = static_cast<int32_t>(step);

        if (const auto ec = set_params(propagation_param); ec != std::error_code{}) {
            return std::unexpected(ec);
        }

        const std::array<ID3D11ShaderResourceView*, 4uz> inputs = {
            nullptr,
            nullptr,
            depth,
            session.resources[current].srv.Get(),
        };
        ID3D11RenderTargetView* const target = session.resources[next].rtv.Get();

        d3d::ctx->PSSetShader(d3d::ps.propagate.Get(), nullptr, 0u);
        d3d::ctx->PSSetShaderResources(0u, static_cast<UINT>(inputs.size()), inputs.data());
        d3d::ctx->OMSetRenderTargets(1u, &target, nullptr);
        d3d::ctx->Draw(3u, 0u);
        d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
        d3d::ctx->PSSetShaderResources(0u, 4u, null_srvs);

        current = next;
        next = (current + 1uz) % session.resources.size();
        distance += step;
    }

    return current;
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

[[nodiscard]] std::error_code Blur(ID3D11RenderTargetView* dst, const std::array<ID3D11ShaderResourceView*, 3uz>& src,
                                   const DrawParam& param, const D3D11_VIEWPORT& vp) {
    static constexpr ID3D11ShaderResourceView* null_srvs[3uz] = {nullptr, nullptr, nullptr};

    HRESULT result;

    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        result = d3d::ctx->Map(d3d::cb.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kMapBufferFailed);
        }

        std::memcpy(mapped.pData, &param, sizeof(param));
        d3d::ctx->Unmap(d3d::cb.Get(), 0u);

        d3d::ctx->PSSetConstantBuffers(0u, 1u, d3d::cb.GetAddressOf());
    }

    d3d::ctx->PSSetShader(d3d::ps.blur.Get(), nullptr, 0u);
    d3d::ctx->PSSetShaderResources(0u, 3u, src.data());
    d3d::ctx->PSSetSamplers(0u, 1u, d3d::smp.GetAddressOf());

    d3d::ctx->OMSetRenderTargets(1u, &dst, nullptr);

    d3d::ctx->RSSetViewports(1u, &vp);

    d3d::ctx->Draw(3u, 0u);

    d3d::ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    d3d::ctx->PSSetShaderResources(0u, 3u, null_srvs);

    return {};
}
}  // namespace

std::error_code make_error_code(Error error) noexcept { return {static_cast<int>(error), kErrorCategory}; }

[[nodiscard]] Context CreateContext(ID3D11Texture2D* dst) { return Context(dst); }

std::error_code Context::Draw(const Source& src, const Param& param) const {
    struct {
        std::array<ComPtr<ID3D11ShaderResourceView>, 2uz> inputs{};
        ComPtr<ID3D11ShaderResourceView> depth = nullptr;
    } view{};

    const auto id = std::bit_cast<uint64_t>(param.id);

    try {
        auto it = d3d::sessions.try_emplace(id).first;
        auto& session = it->second;

        if (session.ctx == nullptr) {
            auto result = CreateSession(param.id);
            if (result.has_value()) {
                session = std::move(*result);
            } else {
                d3d::sessions.erase(id);
                return result.error();
            }
        }

        D3D11_TEXTURE2D_DESC desc{};
        dst_->GetDesc(&desc);

        if (desc.Width != param.id.w || desc.Height != param.id.h) {
            return std::make_error_code(std::errc::invalid_argument);
        }

        for (size_t i = 0uz; i < view.inputs.size(); ++i) {
            const HRESULT result = d3d::device->CreateShaderResourceView(src.inputs[i], nullptr, &view.inputs[i]);
            if (FAILED(result)) {
                return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
            }
        }

        {
            const HRESULT result = d3d::device->CreateShaderResourceView(src.depth, nullptr, &view.depth);
            if (FAILED(result)) {
                return ToErrorCode(result, Error::kCreateShaderResourceViewFailed);
            }
        }

        for (size_t i = 0uz; i < 2uz; ++i) {
            ToABGR8(session.inputs[i].rtv.Get(), view.inputs[i].Get(), session.vp);
        }

        // なぜか2回実行すると精度上がる (Temporal Hint が効いてる？)

        session.ctx->Execute(session.inputs[0uz].buf.get(), session.inputs[1uz].buf.get(),
                             session.outputs[0uz].buf.get(), nullptr, session.costs[0uz].buf.get(), 0u, nullptr,
                             nullptr, 0u, nullptr, param.should_use_temporal_hints ? NV_OF_FALSE : NV_OF_TRUE,
                             session.outputs[1uz].buf.get(), session.costs[1uz].buf.get());

        session.ctx->Execute(session.inputs[0uz].buf.get(), session.inputs[1uz].buf.get(),
                             session.outputs[0uz].buf.get(), nullptr, session.costs[0uz].buf.get(), 0u, nullptr,
                             nullptr, 0u, nullptr, param.should_use_temporal_hints ? NV_OF_FALSE : NV_OF_TRUE,
                             session.outputs[1uz].buf.get(), session.costs[1uz].buf.get());
        const FlowParam flow_param{
            .resolution = {param.id.w, param.id.h},
            .grid_size = session.grid_size,
            .flow_scale = param.scale,
            .shutter = {param.phase, param.phase + param.amount},
            .propagation_step = 0,
        };

        const auto regularized = Regularize(session, flow_param);
        if (!regularized.has_value()) {
            return regularized.error();
        }

        const auto propagated = Propagate(session, *regularized, view.inputs[0uz].Get(), view.depth.Get(), flow_param);
        if (!propagated.has_value()) {
            return propagated.error();
        }

        ComPtr<ID3D11RenderTargetView> rtv;
        const HRESULT result = d3d::device->CreateRenderTargetView(dst_, nullptr, &rtv);
        if (FAILED(result)) {
            return ToErrorCode(result, Error::kCreateRenderTargetViewFailed);
        }

        const float w = static_cast<float>(param.id.w), h = static_cast<float>(param.id.h);
        const float mix = param.mix * 2.0f;
        const DrawParam draw_param{
            .texel = {1.0f / w, 1.0f / h},
            .shutter = {param.phase, param.phase + param.amount},
            .mix = {std::min(2.0f - mix, 1.0f), std::min(mix, 1.0f)},
            .falloff = param.falloff,
            .sample_limit = param.sample_limit,
        };

        const std::array<ID3D11ShaderResourceView*, 3uz> blur_inputs = {
            view.inputs[0uz].Get(),
            session.resources[*propagated].srv.Get(),
            view.depth.Get(),
        };

        return Blur(rtv.Get(), blur_inputs, draw_param, session.vp);
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
}

std::error_code Render(ID3D11Texture2D* dst, FunctionRef callback) {
    const std::lock_guard lock(d3d::mutex);

    if (const auto ec = EnsureResources(dst); ec != std::error_code{}) {
        return ec;
    }

    d3d::ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    d3d::ctx->OMSetDepthStencilState(d3d::dss.Get(), 0u);

    d3d::ctx->RSSetState(nullptr);

    d3d::ctx->GSSetShader(nullptr, nullptr, 0u);

    return callback(CreateContext(dst));
}

void Reset() {
    const std::lock_guard lock(d3d::mutex);
    d3d::Release();
}
}  // namespace blur::scene::renderer
