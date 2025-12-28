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

    // Input layout semantics
    enum class Semantic
    {
        POSITION0,
        COLOR0,
        TEXCOORD0,
        NORMAL0,
        // Can extend with more semantics as needed
    };

    // Input element formats
    enum class InputFormat
    {
        R32_FLOAT,        // 1 float
        R32G32_FLOAT,     // 2 floats (vec2)
        R32G32B32_FLOAT,  // 3 floats (vec3)
        R32G32B32A32_FLOAT, // 4 floats (vec4)
    };

    // Description of a single input element
    struct InputElementDesc
    {
        Semantic semantic;
        InputFormat format;
        size_t offset; // Offset in bytes from start of vertex
    };

    // Description of the input layout (array of elements)
    struct InputLayoutDesc
    {
        std::vector<InputElementDesc> elements;
        size_t stride; // Total size of one vertex in bytes
    };

    // Forward declare for use in VertexInputView
    class InputLayout;
    class ShaderContext;

    // View of vertex input data - provides semantic-based access to vertex attributes
    class VertexInputView
    {
      public:
        VertexInputView( const uint8_t *vertexData, const InputLayout *layout ) 
            : data( vertexData ), layout( layout )
        {
        }

        // Read individual float components by index (e.g., reading X, Y, or Z from a vec3)
        float readFloat1( Semantic semantic, size_t index = 0 ) const;
        // Read vector attributes (index parameter not applicable for these)
        glm::vec2 readFloat2( Semantic semantic ) const;
        glm::vec3 readFloat3( Semantic semantic ) const;
        glm::vec4 readFloat4( Semantic semantic ) const;

      private:
        const uint8_t *data;
        const InputLayout *layout;
    };

    // Input layout - stores the description of how to interpret vertex data
    class InputLayout
    {
      public:
        explicit InputLayout( const InputLayoutDesc &desc ) : desc_( desc )
        {
        }

        const InputLayoutDesc &desc() const
        {
            return desc_;
        }

        size_t stride() const
        {
            return desc_.stride;
        }

      private:
        InputLayoutDesc desc_;
    };

    // Shader context - provides access to constant buffers
    class ShaderContext
    {
      public:
        ShaderContext( const std::vector<std::shared_ptr<Buffer>> &vsBuffers,
                       const std::vector<std::shared_ptr<Buffer>> &psBuffers )
            : vsConstantBuffers( vsBuffers ), psConstantBuffers( psBuffers )
        {
        }

        template <typename T>
        const T *vsCB( size_t slot ) const
        {
            if( slot >= vsConstantBuffers.size() || !vsConstantBuffers[slot] )
                return nullptr;
            return static_cast<const T *>( vsConstantBuffers[slot]->data() );
        }

        template <typename T>
        const T *psCB( size_t slot ) const
        {
            if( slot >= psConstantBuffers.size() || !psConstantBuffers[slot] )
                return nullptr;
            return static_cast<const T *>( psConstantBuffers[slot]->data() );
        }

      private:
        const std::vector<std::shared_ptr<Buffer>> &vsConstantBuffers;
        const std::vector<std::shared_ptr<Buffer>> &psConstantBuffers;
    };

    using VertexShader = std::function<VSOutput( const VertexInputView &, const ShaderContext & )>;
    using PixelShader = std::function<glm::vec4( const PSInput &, const ShaderContext & )>;

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
        R16_UINT, // Для индексных буферов (USHORT/UINT16)
        R32_UINT, // Для индексных буферов (UINT/UINT32)
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
            void setInputLayout( std::shared_ptr<InputLayout> layout );

          private:
            friend class Device;
            IAStage( std::shared_ptr<Device> device ) : parentDevice( device )
            {
            }
            std::weak_ptr<Device> parentDevice;
            std::shared_ptr<Buffer> vertexBuffer;
            std::shared_ptr<Buffer> indexBuffer;
            std::shared_ptr<InputLayout> inputLayout;
            PrimitiveTopology primitiveTopology;
        };

        // VS Vertex Shader stage
        class VSStage
        {
          public:
            void setVertexShader( VertexShader shader );
            void setConstantBuffer( size_t slot, std::shared_ptr<Buffer> buffer );

          private:
            friend class Device;
            VSStage( std::shared_ptr<Device> device ) : parentDevice( device ), constantBuffers( 8 )
            {
            }
            std::weak_ptr<Device> parentDevice;
            VertexShader vertexShader;
            std::vector<std::shared_ptr<Buffer>> constantBuffers;
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
            void setConstantBuffer( size_t slot, std::shared_ptr<Buffer> buffer );

          private:
            friend class Device;
            PSStage( std::shared_ptr<Device> device ) : parentDevice( device ), constantBuffers( 8 )
            {
            }
            std::weak_ptr<Device> parentDevice;
            PixelShader pixelShader;
            std::vector<std::shared_ptr<Buffer>> constantBuffers;
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
        std::shared_ptr<Buffer> createBuffer( size_t elementSize, size_t elementCount, BufferFormat format );

        // Создание input layout
        std::shared_ptr<InputLayout> createInputLayout( const InputLayoutDesc &desc );

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
        void rasterizeTri( const VSOutput &v0, const VSOutput &v1, const VSOutput &v2, const ShaderContext &ctx );
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