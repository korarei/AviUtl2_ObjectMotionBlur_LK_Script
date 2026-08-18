#pragma once

#include <d3d11.h>

#include <array>
#include <cstdint>
#include <system_error>

namespace blur::scene::renderer {
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

struct Source {
    std::array<ID3D11Texture2D*, 2uz> inputs{};
    ID3D11Texture2D* depth = nullptr;
};

struct ID {
    uint16_t w = 0u, h = 0u;
    int32_t preset = 0;
};

static_assert(sizeof(ID) == sizeof(uint64_t));

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

    ~Context() = default;

    [[nodiscard]] std::error_code Draw(const Source& src, const Param& param) const;

  private:
    friend Context CreateContext(ID3D11Texture2D* dst);

    explicit Context(ID3D11Texture2D* dst) : dst_(dst) {}

    ID3D11Texture2D* dst_;
};

// std::function_ref が MSVC に追加され次第置換
class FunctionRef {
  public:
    template <typename F>
        requires(!std::same_as<std::remove_cvref_t<F>, FunctionRef> &&
                 std::convertible_to<std::invoke_result_t<F&, const Context&>, std::error_code>)
    FunctionRef(F&& f) noexcept  // NOLINT(google-explicit-constructor)
        : ptr_(std::addressof(f)), invoke_([](void* ptr, const Context& ctx) -> std::error_code {
              return (*static_cast<std::remove_reference_t<F>*>(ptr))(ctx);
          }) {}

    [[nodiscard]] std::error_code operator()(const Context& ctx) const { return invoke_(ptr_, ctx); }

  private:
    void* ptr_;
    std::error_code (*invoke_)(void*, const Context&);
};

[[nodiscard]] std::error_code Render(ID3D11Texture2D* dst, FunctionRef callback);

void Reset();
}  // namespace blur::scene::renderer

template <>
struct std::is_error_code_enum<blur::scene::renderer::Error> : true_type {};
