#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <expected>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace blur::object::direct3d {
class Renderer {
  public:
    using Result = std::expected<void, std::wstring>;

    struct alignas(16) Transform {
        float position[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float rotation[2] = {0.0f, 0.0f};
        float padding[2] = {0.0f, 0.0f};
    };

    struct alignas(16) Param {
        float transform[3][4] = {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
        float pivot[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float origin[2] = {0.0f, 0.0f};
        float texel[2] = {0.0f, 0.0f};
        float amount = 0.0f;
        int32_t samples = 1;
        float mix[2] = {0.0f, 0.0f};
    };

    class Context {
      public:
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        [[nodiscard]] Result Draw(ID3D11Texture2D* dst, float w, float h, ID3D11Texture2D* src,
                                  const std::vector<Transform>& xforms, const Param& param) const;

      private:
        friend class Renderer;

        explicit Context(Renderer& owner) : owner_(owner) {}
        ~Context() = default;

        Renderer& owner_;
    };

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    Renderer() = default;
    ~Renderer() = default;

    template <class F>
    Result Render(ID3D11Texture2D* tex, F&& f) {
        static_assert(std::is_same_v<std::invoke_result_t<F, Context&>, Result>);

        if (const auto result = Acquire(tex); !result.has_value()) {
            return result;
        }

        Context ctx(*this);
        return std::forward<F>(f)(ctx);
    }

    void Reset();

  private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    [[nodiscard]] Result Acquire(ID3D11Texture2D* tex);

    ComPtr<ID3D11Device> device_ = nullptr;
    ComPtr<ID3D11DeviceContext> ctx_ = nullptr;

    ComPtr<ID3D11VertexShader> vs_ = nullptr;
    ComPtr<ID3D11PixelShader> ps_ = nullptr;
    ComPtr<ID3D11SamplerState> smp_ = nullptr;
    ComPtr<ID3D11Buffer> cb_ = nullptr;

    struct {
        ComPtr<ID3D11Buffer> buffer = nullptr;
        ComPtr<ID3D11ShaderResourceView> srv = nullptr;
        size_t size = 0uz;
    } xforms_{};
};
}  // namespace blur::object::direct3d
