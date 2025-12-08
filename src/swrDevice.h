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

    // Порт вывода (viewport)
    struct Viewport
    {
        int x;
        int y;
        int width;
        int height;
        float minDepth;
        float maxDepth;
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

        // Презентация отрендеренного кадра
        void present( SDL_Renderer *renderer, SDL_Texture *texture );

        // IA Input Assembler stage
        class IAStage
        {
          public:
            void setVertexBuffer( std::shared_ptr<Buffer> buffer );
            void setIndexBuffer( std::shared_ptr<Buffer> buffer );
            void setPrimitiveTopology( PrimitiveTopology topology );

          private:
            friend class Device;
            IAStage( std::shared_ptr<Device> device ) : parentDevice( device )
            {
            }
            std::shared_ptr<Device> parentDevice;
            std::shared_ptr<Buffer> vertexBuffer;
            std::shared_ptr<Buffer> indexBuffer;
            PrimitiveTopology primitiveTopology;
        };

        // VS Vertex Shader stage
        class VSStage
        {
          public:
            void setVertexShader( VertexShader shader );

          private:
            friend class Device;
            VSStage( std::shared_ptr<Device> device ) : parentDevice( device )
            {
            }
            std::shared_ptr<Device> parentDevice;
            VertexShader vertexShader;
        };

        // RS (Rasterizer) stage
        class RSStage
        {
          public:
            // В будущем можно добавить настройки растеризации
            void setViewport( const Viewport &viewport );
            void setCullBackface( bool cull );
            void setWireframe( bool wireframe );

          private:
            friend class Device;
            RSStage( std::shared_ptr<Device> device ) : parentDevice( device )
            {
            }
            std::shared_ptr<Device> parentDevice;
            Viewport viewport;
            bool cullBackface = false;
            bool wireframe = false;
        };

        // PS Pixel Shader stage
        class PSStage
        {
          public:
            void setPixelShader( PixelShader shader );

          private:
            friend class Device;
            PSStage( std::shared_ptr<Device> device ) : parentDevice( device )
            {
            }
            std::shared_ptr<Device> parentDevice;
            PixelShader pixelShader;
        };

        // OM Output Merger stage
        class OMStage
        {
          public:
            void setClearColor( const glm::vec4 &color );
            glm::vec4 clearColor() const;
            // В будущем можно добавить настройки слияния вывода
          private:
            friend class Device;
            OMStage( std::shared_ptr<Device> device ) : parentDevice( device )
            {
            }
            std::shared_ptr<Device> parentDevice;
            glm::vec4 clearColorValue;
        };

        // Доступ к стадиям конвейера
        IAStage &IA()
        {
            return iaStage;
        }
        VSStage &VS()
        {
            return vsStage;
        }
        RSStage &RS()
        {
            return rsStage;
        }
        PSStage &PS()
        {
            return psStage;
        }
        OMStage &OM()
        {
            return omStage;
        }

      private:
        IAStage iaStage;
        VSStage vsStage;
        RSStage rsStage;
        PSStage psStage;
        OMStage omStage;
    };

} // namespace swr