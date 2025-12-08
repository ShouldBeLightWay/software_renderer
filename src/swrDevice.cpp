#include "swrDevice.h"
#include <SDL3/SDL.h>

namespace swr
{

    std::shared_ptr<Device> Device::сreate( size_t width, size_t height )
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
    void Device::present( SDL_Renderer * /*renderer*/, SDL_Texture * /*texture*/ )
    {
    }
    void Device::clear()
    {
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

} // namespace swr
