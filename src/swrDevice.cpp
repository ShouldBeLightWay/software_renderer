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

        const SDL_PixelFormatDetails *pf = SDL_GetPixelFormatDetails( SDL_PIXELFORMAT_RGBA8888 );
        auto vec4ColorToRGBA8 = [pf]( const glm::vec4 &color ) -> std::uint32_t {
            std::uint32_t r = static_cast<std::uint32_t>( glm::clamp( color.r, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t g = static_cast<std::uint32_t>( glm::clamp( color.g, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t b = static_cast<std::uint32_t>( glm::clamp( color.b, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t a = static_cast<std::uint32_t>( glm::clamp( color.a, 0.0f, 1.0f ) * 255.0f );
            // Pack using masks/shifts from pixel format details
            return ( ( r << pf->Rshift ) & pf->Rmask ) | ( ( g << pf->Gshift ) & pf->Gmask ) |
                   ( ( b << pf->Bshift ) & pf->Bmask ) | ( ( a << pf->Ashift ) & pf->Amask );
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
                    dst32[x] = vec4ColorToRGBA8( frameBuffers.colorBuffer[base + x] );
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

    void Device::draw( size_t /*vertexCount*/, size_t /*startVertexLocation*/ )
    {
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
