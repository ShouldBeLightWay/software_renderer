#include <assert.h>

#include "swrDevice.h"

namespace
{
    static inline float clampLineWidth( float lineWidth )
    {
        return std::max( 0.5f, lineWidth );
    }

    static inline const float *findSemanticData( const uint8_t *data, const swr::InputLayout *layout,
                                                 swr::Semantic semantic )
    {
        const auto &desc = layout->desc();
        for( const auto &elem : desc.elements )
        {
            if( elem.semantic == semantic )
                return reinterpret_cast<const float *>( data + elem.offset );
        }

        return nullptr;
    }

    static inline void setConstantBufferSlot( std::vector<std::shared_ptr<swr::Buffer>> &constantBuffers, size_t slot,
                                              std::shared_ptr<swr::Buffer> buffer )
    {
        if( slot >= constantBuffers.size() )
            constantBuffers.resize( slot + 1 );
        constantBuffers[slot] = std::move( buffer );
    }

} // unnamed namespace

namespace swr
{

    std::shared_ptr<Device> Device::create( size_t width, size_t height, PresentCallback presentCallback )
    {
        assert( presentCallback && "Present callback must be provided" );

        auto dev = std::shared_ptr<Device>( new Device( width, height, std::move( presentCallback ) ) );
        dev->initStages( dev );
        return dev;
    }

    Device::~Device() = default;

    std::shared_ptr<Buffer> Device::createBuffer( size_t elementSize, size_t elementCount, BufferFormat format )
    {
        std::weak_ptr<Device> wself = shared_from_this();
        Buffer *raw = new Buffer( elementSize, elementCount, format );
        auto deleter = [wself]( Buffer *p ) { delete p; };
        return std::shared_ptr<Buffer>( raw, std::move( deleter ) );
    }

    std::shared_ptr<InputLayout> Device::createInputLayout( const InputLayoutDesc &desc )
    {
        return std::make_shared<InputLayout>( desc );
    }

    float VertexInputView::readFloat1( Semantic semantic, size_t index ) const
    {
        const float *ptr = findSemanticData( data, layout, semantic );
        if( ptr )
            return ptr[index];
        return 0.0f;
    }

    glm::vec2 VertexInputView::readFloat2( Semantic semantic ) const
    {
        const float *ptr = findSemanticData( data, layout, semantic );
        if( ptr )
            return glm::vec2( ptr[0], ptr[1] );
        return glm::vec2( 0.0f );
    }

    glm::vec3 VertexInputView::readFloat3( Semantic semantic ) const
    {
        const float *ptr = findSemanticData( data, layout, semantic );
        if( ptr )
            return glm::vec3( ptr[0], ptr[1], ptr[2] );
        return glm::vec3( 0.0f );
    }

    glm::vec4 VertexInputView::readFloat4( Semantic semantic ) const
    {
        const float *ptr = findSemanticData( data, layout, semantic );
        if( ptr )
            return glm::vec4( ptr[0], ptr[1], ptr[2], ptr[3] );
        return glm::vec4( 0.0f );
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

    void Device::present()
    {
        assert( frameWidth * frameHeight == frameBuffers.colorBuffer.size() );
        assert( presentCallback && "Present callback must be set" );

        presentCallback( frameBuffers.colorBuffer, frameWidth, frameHeight );
    }

    void Device::clear()
    {
        auto clearColor = omStage.clearColor();
        auto clearDepth = omStage.depthClearValue();
        std::fill( frameBuffers.colorBuffer.begin(), frameBuffers.colorBuffer.end(), clearColor );
        std::fill( frameBuffers.depthBuffer.begin(), frameBuffers.depthBuffer.end(), clearDepth );
    }

    bool Device::prepareDrawState( DrawState &drawState ) const
    {
        drawState.vertexBuffer = iaStage.vertexBuffer;
        if( !drawState.vertexBuffer )
        {
            assert( false && "No vertex buffer set" );
            return false;
        }

        drawState.inputLayout = iaStage.inputLayout;
        if( !drawState.inputLayout )
        {
            assert( false && "No input layout set" );
            return false;
        }

        if( !vsStage.vertexShader )
        {
            assert( false && "No vertex shader set" );
            return false;
        }

        drawState.vertexData = static_cast<const uint8_t *>( drawState.vertexBuffer->data() );
        drawState.stride = drawState.inputLayout->stride();
        drawState.vertexShader = vsStage.vertexShader;
        return true;
    }

    void Device::shadeAndWritePixel( size_t fbIndex, float depth, const PSInput &psIn, const ShaderContext &ctx )
    {
        glm::vec4 outColor = psStage.pixelShader( psIn, ctx );
        frameBuffers.colorBuffer[fbIndex] = outColor;
        frameBuffers.depthBuffer[fbIndex] = depth;
    }

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

    void Device::IAStage::setInputLayout( std::shared_ptr<InputLayout> layout )
    {
        inputLayout = std::move( layout );
    }

    void Device::VSStage::setVertexShader( VertexShader shader )
    {
        vertexShader = std::move( shader );
    }

    void Device::VSStage::setConstantBuffer( size_t slot, std::shared_ptr<Buffer> buffer )
    {
        setConstantBufferSlot( constantBuffers, slot, std::move( buffer ) );
    }

    void Device::RSStage::setViewport( const Viewport &vp )
    {
        viewport = vp;
    }

    void Device::RSStage::setCullBackface( bool cull )
    {
        cullBackface = cull;
    }

    void Device::RSStage::setLineWidth( float lw )
    {
        lineWidth = clampLineWidth( lw );
    }

    void Device::RSStage::setWireframe( bool wf )
    {
        wireframe = wf;
    }

    void Device::PSStage::setPixelShader( PixelShader shader )
    {
        pixelShader = std::move( shader );
    }

    void Device::PSStage::setConstantBuffer( size_t slot, std::shared_ptr<Buffer> buffer )
    {
        setConstantBufferSlot( constantBuffers, slot, std::move( buffer ) );
    }

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