#include <assert.h>
#include <iostream>

#include "swrDevice.h"
#include <SDL3/SDL.h>

namespace
{
    struct TextureLock
    {
        SDL_Texture *tex{ nullptr };
        void *pixels{ nullptr };
        int pitch{ 0 };
        bool ok{ false };

        explicit TextureLock( SDL_Texture *t, const SDL_Rect *rect = nullptr ) : tex( t )
        {
            ok = SDL_LockTexture( tex, rect, &pixels, &pitch );
        }

        ~TextureLock()
        {
            if( ok )
                SDL_UnlockTexture( tex );
        }

        TextureLock( const TextureLock & ) = delete;
        TextureLock &operator=( const TextureLock & ) = delete;
    };
} // unnamed namespace

namespace swr
{

    std::shared_ptr<Device> Device::create( size_t width, size_t height )
    {
        // Создаём shared_ptr<Device>, затем инициализируем стадии
        auto dev = std::shared_ptr<Device>( new Device( width, height ) );
        dev->initStages( dev );
        return dev;
    }

    Device::~Device() = default;

    std::shared_ptr<Buffer> Device::createBuffer( size_t elementSize, size_t elementCount )
    {
        // Делетер захватывает weak_ptr<Device>, чтобы избежать продления жизни устройства
        std::weak_ptr<Device> wself = shared_from_this();
        Buffer *raw = new Buffer( elementSize, elementCount );
        auto deleter = [wself]( Buffer *p ) {
            // Если устройство ещё живо, тут можно выполнить внутреннюю очистку
            // if (auto self = wself.lock()) { /* self->onBufferDestroy(p); */ }
            delete p;
        };
        return std::shared_ptr<Buffer>( raw, std::move( deleter ) );
    }

    void Device::resize( size_t width, size_t height )
    {
        if( width == 0 || height == 0 )
            return;
        frameWidth = width;
        frameHeight = height;
        frameBuffers.colorBuffer.assign( width * height, omStage.clearColor() );
        frameBuffers.depthBuffer.assign( width * height, omStage.depthClearValue() );
    }

    // Заглушки стадий (интерфейсные методы) — реализации по мере развития
    void Device::present( SDL_Renderer *renderer, SDL_Texture *texture )
    {
        /*
        Нужно:

        SDL_LockTexture

        Скопировать glm::vec4 → RGBA8

        SDL_UnlockTexture

        SDL_RenderTexture

        SDL_RenderPresent
        */
        assert( renderer != nullptr );
        assert( texture != nullptr );
        assert( frameWidth * frameHeight == frameBuffers.colorBuffer.size() );

        static const SDL_PixelFormatDetails *pf = SDL_GetPixelFormatDetails( SDL_PIXELFORMAT_RGBA8888 );
        auto vec4ColorToRGBA8 = []( const glm::vec4 &color, const SDL_PixelFormatDetails *pfmt ) -> std::uint32_t {
            std::uint32_t r = static_cast<std::uint32_t>( glm::clamp( color.r, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t g = static_cast<std::uint32_t>( glm::clamp( color.g, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t b = static_cast<std::uint32_t>( glm::clamp( color.b, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t a = static_cast<std::uint32_t>( glm::clamp( color.a, 0.0f, 1.0f ) * 255.0f );
            // Pack using masks/shifts from pixel format details
            return ( ( r << pfmt->Rshift ) & pfmt->Rmask ) | ( ( g << pfmt->Gshift ) & pfmt->Gmask ) |
                   ( ( b << pfmt->Bshift ) & pfmt->Bmask ) | ( ( a << pfmt->Ashift ) & pfmt->Amask );
        };

        size_t width = frameWidth;
        size_t height = frameHeight;
        // Обновление текстуры через Lock/Unlock без доп. аллокаций
        {
            TextureLock lock( texture );
            if( !lock.ok )
            {
                std::cerr << "SDL_LockTexture failed: " << SDL_GetError() << std::endl;
                return;
            }
            assert( lock.pixels != nullptr );
            assert( lock.pitch >= static_cast<int>( width ) * 4 );

            // Пишем построчно с учётом pitch
            auto *row = static_cast<std::uint8_t *>( lock.pixels );
            for( size_t y = 0; y < height; ++y )
            {
                auto *dst32 = reinterpret_cast<std::uint32_t *>( row );
                const size_t base = y * width;
                for( size_t x = 0; x < width; ++x )
                {
                    dst32[x] = vec4ColorToRGBA8( frameBuffers.colorBuffer[base + x], pf );
                }
                row += lock.pitch;
            }
            // lock выходит из области видимости здесь и вызывает SDL_UnlockTexture
        }

        // Сброс вьюпорта/масштаба и явное очищение фона в чёрный
        SDL_SetRenderViewport( renderer, nullptr );
        SDL_SetRenderScale( renderer, 1.0f, 1.0f );
        SDL_SetRenderDrawColor( renderer, 0, 0, 0, 255 );
        SDL_RenderClear( renderer );
        SDL_FRect dst{ 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) };
        SDL_RenderTexture( renderer, texture, nullptr, &dst );
        SDL_RenderPresent( renderer );
    }

    void Device::clear()
    {
        auto clearColor = omStage.clearColor();
        auto clearDepth = omStage.depthClearValue();
        std::fill( frameBuffers.colorBuffer.begin(), frameBuffers.colorBuffer.end(), clearColor );
        std::fill( frameBuffers.depthBuffer.begin(), frameBuffers.depthBuffer.end(), clearDepth );
    }

    // Вычисление ориентированной площади треугольника из которой берутся барицентрические координаты
    static inline float edgeFunction( const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c )
    {
        return ( c.x - a.x ) * ( b.y - a.y ) - ( c.y - a.y ) * ( b.x - a.x );
    }

    void Device::draw( size_t vertexCount, size_t startVertexLocation )
    {
        // IA - забираем VB
        if( iaStage.primitiveTopology != PrimitiveTopology::TriangleList )
        {
            assert( false && "Unsupported primitive topology" );
            return;
        }
        auto vb = iaStage.vertexBuffer;
        if( !vb )
        {
            assert( false && "No vertex buffer set" );
            return;
        }

        const Vertex *vertices = static_cast<const Vertex *>( vb->data() );
        size_t vertexSize = vb->elementSize();

        // VS - трансформируем вершины прогоняя их через шейдер
        std::vector<VSOutput> vsOut( vertexCount );

        for( size_t i = 0; i < vertexCount; ++i )
        {
            const Vertex &inVertex = *reinterpret_cast<const Vertex *>( reinterpret_cast<const uint8_t *>( vertices ) +
                                                                        ( startVertexLocation + i ) * vertexSize );
            vsOut[i] = vsStage.vertexShader( inVertex );
        }

        // Primitive assembly: triangle list
        // Получаем viewport (если не задан, используем весь кадр)
        Viewport vp{ 0, 0, static_cast<int>( frameWidth ), static_cast<int>( frameHeight ), 0.0f, 1.0f };
        if( rsStage.viewport.width > 0 && rsStage.viewport.height > 0 )
            vp = rsStage.viewport;
        const float vpW = static_cast<float>( vp.width );
        const float vpH = static_cast<float>( vp.height );

        for( size_t i = 0; i + 2 < vertexCount; i += 3 )
        {
            // Take primitive vertices
            const VSOutput &v0 = vsOut[i];
            const VSOutput &v1 = vsOut[i + 1];
            const VSOutput &v2 = vsOut[i + 2];

            // Clip to NDC space
            auto p0 = glm::vec3( v0.position ) / v0.position.w;
            auto p1 = glm::vec3( v1.position ) / v1.position.w;
            auto p2 = glm::vec3( v2.position ) / v2.position.w;

            // NDC to Screen space (viewport transform)
            auto ndcToViewport = [&]( const glm::vec3 &ndc ) {
                float sx = ( ndc.x * 0.5f + 0.5f ) * vpW + static_cast<float>( vp.x );
                float sy = ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * vpH + static_cast<float>( vp.y );
                return glm::vec2( sx, sy );
            };
            glm::vec2 s0 = ndcToViewport( p0 );
            glm::vec2 s1 = ndcToViewport( p1 );
            glm::vec2 s2 = ndcToViewport( p2 );

            // Boundig box
            int minX = static_cast<int>( glm::floor( glm::min( glm::min( s0.x, s1.x ), s2.x ) ) );
            int maxX = static_cast<int>( glm::ceil( glm::max( glm::max( s0.x, s1.x ), s2.x ) ) );
            int minY = static_cast<int>( glm::floor( glm::min( glm::min( s0.y, s1.y ), s2.y ) ) );
            int maxY = static_cast<int>( glm::ceil( glm::max( glm::max( s0.y, s1.y ), s2.y ) ) );

            // Отсечение по viewport прямоугольнику
            minX = std::max( minX, vp.x );
            minY = std::max( minY, vp.y );
            maxX = std::min( maxX, vp.x + vp.width - 1 );
            maxY = std::min( maxY, vp.y + vp.height - 1 );

            // Полная площадь треугольника
            float area = edgeFunction( s0, s1, s2 );
            if( area == 0.0f )
                continue; // Вырожденный треугольник

            // RS: Отсечение задних граней (простая политика: area>0 считаем фронт-фейс)
            if( rsStage.cullBackface )
            {
                if( area < 0.0f )
                    continue;
            }

            // Растеризация внутри ограничивающего прямоугольника
            for( int y = minY; y <= maxY; ++y )
            {
                for( int x = minX; x <= maxX; ++x )
                {
                    glm::vec2 p( static_cast<float>( x ) + 0.5f, static_cast<float>( y ) + 0.5f );

                    // Барицентрические координаты
                    float w0 = edgeFunction( s1, s2, p );
                    float w1 = edgeFunction( s2, s0, p );
                    float w2 = edgeFunction( s0, s1, p );

                    // Если точка проходит внутренний тест или режим wireframe — пиксели на ребре
                    bool inside = ( area > 0.0f && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f ) ||
                                  ( area < 0.0f && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f );

                    // Wireframe: рисуем только пиксели на границе (вблизи ребра)
                    bool onEdge = false;
                    if( rsStage.wireframe )
                    {
                        // Связь: |edgeFunction(e,p)| = |e| * distance(p, edge)
                        // Поэтому сравниваем с длиной ребра * допуск_в_пикселях
                        const float epsPixels = 0.75f; // толщина линии ~1px
                        float L0 = glm::length( s2 - s1 );
                        float L1 = glm::length( s0 - s2 );
                        float L2 = glm::length( s1 - s0 );
                        onEdge = ( std::abs( w0 ) <= L0 * epsPixels ) || ( std::abs( w1 ) <= L1 * epsPixels ) ||
                                 ( std::abs( w2 ) <= L2 * epsPixels );
                    }

                    if( inside && ( !rsStage.wireframe || onEdge ) )
                    {
                        w0 /= area;
                        w1 /= area;
                        w2 /= area;

                        // Перспективно-корректная интерполяция: используем 1/w как вес
                        float invW0 = 1.0f / v0.position.w;
                        float invW1 = 1.0f / v1.position.w;
                        float invW2 = 1.0f / v2.position.w;
                        float denom = w0 * invW0 + w1 * invW1 + w2 * invW2;
                        if( denom <= 0.0f )
                            continue;

                        // Интерполяция глубины (z_ndc) с делением на общий знаменатель
                        float depth = ( w0 * p0.z + w1 * p1.z + w2 * p2.z ) / denom;

                        size_t fbIndex = static_cast<size_t>( y ) * frameWidth + static_cast<size_t>( x );
                        // Тест глубины
                        if( depth < frameBuffers.depthBuffer[fbIndex] )
                        {
                            // PS - формируем входные данные и вызываем пиксельный шейдер
                            PSInput psIn;
                            // Цвет/любые атрибуты тоже интерполируем перспективно-корректно
                            glm::vec3 colorNum = w0 * v0.color * invW0 + w1 * v1.color * invW1 + w2 * v2.color * invW2;
                            psIn.color = colorNum / denom;
                            psIn.barycentric = glm::vec3( w0, w1, w2 );
                            psIn.depth = depth;

                            glm::vec4 outColor = psStage.pixelShader( psIn );

                            // Запись в буферы
                            frameBuffers.colorBuffer[fbIndex] = outColor;
                            frameBuffers.depthBuffer[fbIndex] = depth;
                        }
                    }
                }
            }
        }
    }

    void Device::drawIndexed( size_t /*indexCount*/, size_t /*startIndexLocation*/, size_t /*baseVertexLocation*/ )
    {
    }

    // IAStage
    void Device::IAStage::setVertexBuffer( std::shared_ptr<Buffer> buffer )
    {
        vertexBuffer = std::move( buffer );
    }
    void Device::IAStage::setIndexBuffer( std::shared_ptr<Buffer> buffer )
    {
        indexBuffer = std::move( buffer );
    }
    void Device::IAStage::setPrimitiveTopology( PrimitiveTopology topology )
    {
        primitiveTopology = topology;
    }

    // VSStage
    void Device::VSStage::setVertexShader( VertexShader shader )
    {
        vertexShader = std::move( shader );
    }

    // RSStage
    void Device::RSStage::setViewport( const Viewport &vp )
    {
        viewport = vp;
    }
    void Device::RSStage::setCullBackface( bool cull )
    {
        cullBackface = cull;
    }
    void Device::RSStage::setWireframe( bool wf )
    {
        wireframe = wf;
    }

    // PSStage
    void Device::PSStage::setPixelShader( PixelShader shader )
    {
        pixelShader = std::move( shader );
    }

    // OMStage
    void Device::OMStage::setClearColor( const glm::vec4 &color )
    {
        clearColorValue = color;
    }

    glm::vec4 Device::OMStage::clearColor() const
    {
        return clearColorValue;
    }

    void Device::OMStage::setDepthClearValue( float depth )
    {
        depthClear = depth;
    }

    float Device::OMStage::depthClearValue() const
    {
        return depthClear;
    }

} // namespace swr
