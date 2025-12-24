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
        // Создание устройства только через фабрику
        static std::shared_ptr<Device> create( size_t width, size_t height );

        ~Device();
        Device( const Device & ) = delete;
        Device &operator=( const Device & ) = delete;

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
            std::weak_ptr<Device> parentDevice;
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
            std::weak_ptr<Device> parentDevice;
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
            std::weak_ptr<Device> parentDevice;
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
            std::weak_ptr<Device> parentDevice;
            PixelShader pixelShader;
        };

        // OM Output Merger stage
        class OMStage
        {
          public:
            void setClearColor( const glm::vec4 &color );
            glm::vec4 clearColor() const;
            void setDepthClearValue( float depth );
            float depthClearValue() const;
            // В будущем можно добавить настройки слияния вывода
          private:
            friend class Device;
            OMStage( std::shared_ptr<Device> device ) : parentDevice( device )
            {
            }
            std::weak_ptr<Device> parentDevice;
            glm::vec4 clearColorValue;
            float depthClear = 1.0f;
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

        // Создание буфера (управляется shared_ptr с кастомным делетером)
        std::shared_ptr<Buffer> createBuffer( size_t elementSize, size_t elementCount );

        size_t deviceFrameWidth() const
        {
            return frameWidth;
        }
        size_t deviceFrameHeight() const
        {
            return frameHeight;
        }

        // Resize internal frame buffers (in pixels)
        void resize( size_t width, size_t height );

        // Презентация отрендеренного кадра
        void present( SDL_Renderer *renderer, SDL_Texture *texture );

        // Управление рендерингом кадра
        void clear();
        void draw( size_t vertexCount, size_t startVertexLocation );
        void drawIndexed( size_t indexCount, size_t startIndexLocation, size_t baseVertexLocation );

      private:
        // Внутренний метод растеризации одного треугольника (после VS)
        void rasterizeTri( const VSOutput &v0, const VSOutput &v1, const VSOutput &v2 );
        // Приватный конструктор: инициализация внутренних буферов, без shared_from_this()
        Device( size_t width, size_t height )
            : iaStage( std::shared_ptr<Device>() ), vsStage( std::shared_ptr<Device>() ),
              rsStage( std::shared_ptr<Device>() ), psStage( std::shared_ptr<Device>() ),
              omStage( std::shared_ptr<Device>() ), frameWidth( width ), frameHeight( height )
        {
            frameBuffers.colorBuffer.resize( width * height, glm::vec4( 0.0f ) );
            frameBuffers.depthBuffer.resize( width * height, 1.0f );
        }

        // Инициализация стадий после создания shared_ptr<Device>
        void initStages( const std::shared_ptr<Device> &self )
        {
            iaStage = IAStage( self );
            vsStage = VSStage( self );
            rsStage = RSStage( self );
            psStage = PSStage( self );
            omStage = OMStage( self );
        }
        IAStage iaStage;
        VSStage vsStage;
        RSStage rsStage;
        PSStage psStage;
        OMStage omStage;

        struct InternalFrameBuffers
        {
            std::vector<glm::vec4> colorBuffer; // RGBA color buffer
            std::vector<float> depthBuffer;     // Depth buffer
        };

        InternalFrameBuffers frameBuffers;
        size_t frameWidth;
        size_t frameHeight;
    };

} // namespace swr