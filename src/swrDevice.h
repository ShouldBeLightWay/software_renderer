#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include <vector>

#include <glm/glm.hpp>

// SDL3 forward decl что бы не тащить SDL3 сюда
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

#include "swrBuffer.h"

namespace swr
{
    // Базовые типы данных конвейера
    struct Vertex
    {
        glm::vec3 position; // Позиция object space
        glm::vec3 color;    // Цвет вершины, RGB 0..1
    };

    struct VSOutput
    {
        glm::vec4 position; // Позиция в  clip space (после world-view-projection)
        glm::vec3 color;    // Цвет вершины, RGB 0..1
    };

    struct PSInput
    {
        glm::vec3 color;       // Цвет вершины, RGB 0..1
        glm::vec3 barycentric; // Барицентрические координаты
        float depth;           // Глубина пикселя
    };

    using VertexShader = std::function<VSOutput( const Vertex & )>;
    using PixelShader = std::function<glm::vec4( const PSInput & )>;

    // Перечисление топологий примитивов
    enum class PrimitiveTopology
    {
        TriangleList,
        // В будущем можно добавить другие топологии
    };

    // Перечислеение форматов буферов (в будущем)
    enum class BufferFormat
    {
        Unknown,
        R8G8B8A8_UNORM,
        D24_UNORM_S8_UINT,
        // Добавить другие форматы по мере необходимости
    };

    // Устройство рендеринга
    class Device : public std::enable_shared_from_this<Device>
    {
      public:
        Device( size_t width, size_t height );
        ~Device();
        Device( const Device & ) = delete;
        Device &operator=( const Device & ) = delete;

        // Создание буфера (управляется shared_ptr с кастомным делетером)
        std::shared_ptr<Buffer> createBuffer( size_t elementSize, size_t elementCount );

        // Установка рендер-таргета (окна SDL)
        void setRenderTarget( SDL_Window *window );

        // Основной метод рендеринга сцены
        void draw( PrimitiveTopology topology, Buffer *vertexBuffer, size_t vertexCount, const VertexShader &vs,
                   const PixelShader &ps );

        // Презентация отрендеренного кадра
        void present();
    };

} // namespace swr