#pragma once

#include <d3d11.h>

#include <array>
#include <cstdint>
#include <system_error>
#include <vector>

namespace blur::object::renderer {
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

using Float2x3 = std::array<std::array<float, 3uz>, 2uz>;

static_assert(sizeof(Float2x3) == sizeof(float) * 6uz);

struct alignas(16) Param {
    float transform[2uz][4uz] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}};
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

    ~Context() = default;

    [[nodiscard]] std::error_code Draw(ID3D11Texture2D* src, const std::vector<Float2x3>& subframe_xforms,
                                       const Param& param) const;

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
}  // namespace blur::object::renderer

template <>
struct std::is_error_code_enum<blur::object::renderer::Error> : true_type {};
