#include <assert.h>

#include "swrDevice.h"

namespace
{
    static inline float clampLineWidth( float lineWidth )
    {
        return std::max( 0.5f, lineWidth );
    }

    struct RasterBounds
    {
        int minX;
        int maxX;
        int minY;
        int maxY;
    };

    static inline swr::Viewport resolveViewport( const swr::Viewport &configuredViewport, size_t frameWidth,
                                                 size_t frameHeight )
    {
        if( configuredViewport.width > 0 && configuredViewport.height > 0 )
            return configuredViewport;

        return swr::Viewport{ 0, 0, static_cast<int>( frameWidth ), static_cast<int>( frameHeight ), 0.0f, 1.0f };
    }

    static inline glm::vec3 perspectiveDivide( const swr::VSOutput &vertex )
    {
        return glm::vec3( vertex.position ) / vertex.position.w;
    }

    static inline glm::vec2 ndcToViewport( const glm::vec3 &ndc, const swr::Viewport &vp )
    {
        float sx = ( ndc.x * 0.5f + 0.5f ) * static_cast<float>( vp.width ) + static_cast<float>( vp.x );
        float sy = ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * static_cast<float>( vp.height ) + static_cast<float>( vp.y );
        return glm::vec2( sx, sy );
    }

    static inline bool clipBoundsToViewport( RasterBounds &bounds, const swr::Viewport &vp )
    {
        bounds.minX = std::max( bounds.minX, vp.x );
        bounds.minY = std::max( bounds.minY, vp.y );
        bounds.maxX = std::min( bounds.maxX, vp.x + vp.width - 1 );
        bounds.maxY = std::min( bounds.maxY, vp.y + vp.height - 1 );
        return bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY;
    }

    static inline size_t frameBufferIndex( int x, int y, size_t frameWidth )
    {
        return static_cast<size_t>( y ) * frameWidth + static_cast<size_t>( x );
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

    template<typename FetchVertexFn, typename EmitPrimitiveFn>
    void assemblePrimitives( swr::PrimitiveTopology topology, size_t vertexCount, FetchVertexFn &&fetchVertex,
                             EmitPrimitiveFn &&emitPrimitive )
    {
        auto emitPoint = [&]( size_t i0 ) {
            swr::AssembledPrimitive primitive;
            primitive.kind = swr::PrimitiveKind::Point;
            primitive.vertexCount = 1;
            primitive.vertices[0] = fetchVertex( i0 );
            emitPrimitive( primitive );
        };

        auto emitLine = [&]( size_t i0, size_t i1 ) {
            swr::AssembledPrimitive primitive;
            primitive.kind = swr::PrimitiveKind::Line;
            primitive.vertexCount = 2;
            primitive.vertices[0] = fetchVertex( i0 );
            primitive.vertices[1] = fetchVertex( i1 );
            emitPrimitive( primitive );
        };

        auto emitTriangle = [&]( size_t i0, size_t i1, size_t i2 ) {
            swr::AssembledPrimitive primitive;
            primitive.kind = swr::PrimitiveKind::Triangle;
            primitive.vertexCount = 3;
            primitive.vertices[0] = fetchVertex( i0 );
            primitive.vertices[1] = fetchVertex( i1 );
            primitive.vertices[2] = fetchVertex( i2 );
            emitPrimitive( primitive );
        };

        switch( topology )
        {
        case swr::PrimitiveTopology::PointList:
            for( size_t i = 0; i < vertexCount; ++i )
                emitPoint( i );
            break;

        case swr::PrimitiveTopology::LineList:
            for( size_t i = 0; i + 1 < vertexCount; i += 2 )
                emitLine( i, i + 1 );
            break;

        case swr::PrimitiveTopology::LineStrip:
            for( size_t i = 0; i + 1 < vertexCount; ++i )
                emitLine( i, i + 1 );
            break;

        case swr::PrimitiveTopology::TriangleList:
            for( size_t i = 0; i + 2 < vertexCount; i += 3 )
                emitTriangle( i, i + 1, i + 2 );
            break;

        case swr::PrimitiveTopology::TriangleStrip:
            for( size_t i = 0; i + 2 < vertexCount; ++i )
            {
                if( ( i & 1 ) == 0 )
                    emitTriangle( i, i + 1, i + 2 );
                else
                    emitTriangle( i + 1, i, i + 2 );
            }
            break;

        case swr::PrimitiveTopology::TriangleFan:
            for( size_t i = 1; i + 1 < vertexCount; ++i )
                emitTriangle( 0, i, i + 1 );
            break;

        default:
            assert( false && "Unsupported primitive topology" );
            break;
        }
    }

    template<typename EmitPrimitiveFn>
    void emitNonIndexedPrimitives( const std::uint8_t *vertexData, size_t stride, size_t startVertexLocation,
                                   size_t vertexCount, const swr::InputLayout *layout, const swr::ShaderContext &ctx,
                                   const swr::VertexShader &vertexShader, swr::PrimitiveTopology topology,
                                   EmitPrimitiveFn &&emitPrimitive )
    {
        auto fetchVertex = [&]( size_t logicalIndex ) -> swr::VSOutput {
            const uint8_t *vertexBytes = vertexData + ( startVertexLocation + logicalIndex ) * stride;
            swr::VertexInputView inputView( vertexBytes, layout );
            return vertexShader( inputView, ctx );
        };

        assemblePrimitives( topology, vertexCount, fetchVertex, std::forward<EmitPrimitiveFn>( emitPrimitive ) );
    }

    template<typename EmitPrimitiveFn>
    void emitIndexedPrimitives( const std::uint8_t *idxBytes, swr::BufferFormat idxFmt, size_t idxElemSize,
                                const std::uint8_t *vertexData, size_t stride, size_t startIndexLocation,
                                size_t indexCount, size_t baseVertexLocation, const swr::InputLayout *layout,
                                const swr::ShaderContext &ctx, const swr::VertexShader &vertexShader,
                                swr::PrimitiveTopology topology, EmitPrimitiveFn &&emitPrimitive )
    {
        auto readIndex = [&]( size_t logicalIndex ) -> uint32_t {
            size_t offset = ( startIndexLocation + logicalIndex ) * idxElemSize;
            if( idxFmt == swr::BufferFormat::R16_UINT )
            {
                const uint16_t *p = reinterpret_cast<const uint16_t *>( idxBytes + offset );
                return static_cast<uint32_t>( *p );
            }

            const uint32_t *p = reinterpret_cast<const uint32_t *>( idxBytes + offset );
            return *p;
        };

        auto fetchVertex = [&]( size_t logicalIndex ) -> swr::VSOutput {
            uint32_t vertexIndex = readIndex( logicalIndex ) + static_cast<uint32_t>( baseVertexLocation );
            const uint8_t *vertexBytes = vertexData + static_cast<size_t>( vertexIndex ) * stride;
            swr::VertexInputView inputView( vertexBytes, layout );
            return vertexShader( inputView, ctx );
        };

        assemblePrimitives( topology, indexCount, fetchVertex, std::forward<EmitPrimitiveFn>( emitPrimitive ) );
    }

} // unnamed namespace

namespace swr
{

    std::shared_ptr<Device> Device::create( size_t width, size_t height, PresentCallback presentCallback )
    {
        // Создаём shared_ptr<Device>, затем инициализируем стадии
        assert( presentCallback && "Present callback must be provided" );

        auto dev = std::shared_ptr<Device>( new Device( width, height, std::move( presentCallback ) ) );
        dev->initStages( dev );
        return dev;
    }

    Device::~Device() = default;

    std::shared_ptr<Buffer> Device::createBuffer( size_t elementSize, size_t elementCount, BufferFormat format )
    {
        // Делетер захватывает weak_ptr<Device>, чтобы избежать продления жизни устройства
        std::weak_ptr<Device> wself = shared_from_this();
        Buffer *raw = new Buffer( elementSize, elementCount, format );
        auto deleter = [wself]( Buffer *p ) {
            // Если устройство ещё живо, тут можно выполнить внутреннюю очистку
            // if (auto self = wself.lock()) { /* self->onBufferDestroy(p); */ }
            delete p;
        };
        return std::shared_ptr<Buffer>( raw, std::move( deleter ) );
    }

    std::shared_ptr<InputLayout> Device::createInputLayout( const InputLayoutDesc &desc )
    {
        return std::make_shared<InputLayout>( desc );
    }

    // VertexInputView implementations
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

    // Вычисление ориентированной площади треугольника из которой берутся барицентрические координаты
    static inline float edgeFunction( const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c )
    {
        return ( c.x - a.x ) * ( b.y - a.y ) - ( c.y - a.y ) * ( b.x - a.x );
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

    void Device::draw( size_t vertexCount, size_t startVertexLocation )
    {
        DrawState drawState( vsStage.constantBuffers, psStage.constantBuffers );
        if( !prepareDrawState( drawState ) )
            return;

        emitNonIndexedPrimitives(
            drawState.vertexData, drawState.stride, startVertexLocation, vertexCount, drawState.inputLayout.get(),
            drawState.shaderContext, drawState.vertexShader, iaStage.primitiveTopology,
            [&]( const AssembledPrimitive &primitive ) { rasterizePrimitive( primitive, drawState.shaderContext ); } );
    }

    void Device::drawIndexed( size_t indexCount, size_t startIndexLocation, size_t baseVertexLocation )
    {
        DrawState drawState( vsStage.constantBuffers, psStage.constantBuffers );
        if( !prepareDrawState( drawState ) )
            return;

        auto ib = iaStage.indexBuffer;
        if( !ib )
        {
            assert( false && "Index buffer not set" );
            return;
        }

        // Поддерживаем форматы индексов R16_UINT и R32_UINT
        BufferFormat idxFmt = ib->format();
        size_t idxElemSize = ib->elementSize();
        if( !( ( idxFmt == BufferFormat::R16_UINT && idxElemSize == 2 ) ||
               ( idxFmt == BufferFormat::R32_UINT && idxElemSize == 4 ) ) )
        {
            assert( false && "Unsupported index buffer format/elementSize" );
            return;
        }

        const uint8_t *idxBytes = static_cast<const uint8_t *>( ib->data() );

        emitIndexedPrimitives(
            idxBytes, idxFmt, idxElemSize, drawState.vertexData, drawState.stride, startIndexLocation, indexCount,
            baseVertexLocation, drawState.inputLayout.get(), drawState.shaderContext, drawState.vertexShader,
            iaStage.primitiveTopology,
            [&]( const AssembledPrimitive &primitive ) { rasterizePrimitive( primitive, drawState.shaderContext ); } );
    }

    void Device::rasterizePrimitive( const AssembledPrimitive &primitive, const ShaderContext &ctx )
    {
        switch( primitive.kind )
        {
        case PrimitiveKind::Point:
            rasterizePoint( primitive.vertices[0], ctx );
            break;

        case PrimitiveKind::Line:
            rasterizeLine( primitive.vertices[0], primitive.vertices[1], ctx );
            break;

        case PrimitiveKind::Triangle:
            rasterizeTriangle( primitive.vertices[0], primitive.vertices[1], primitive.vertices[2], ctx );
            break;

        default:
            assert( false && "Unsupported primitive kind" );
            break;
        }
    }

    void Device::rasterizePoint( const VSOutput &vertex, const ShaderContext &ctx )
    {
        Viewport vp = resolveViewport( rsStage.viewport, frameWidth, frameHeight );

        glm::vec3 ndc = perspectiveDivide( vertex );
        glm::vec2 screenPos = ndcToViewport( ndc, vp );

        int pixelX = static_cast<int>( glm::floor( screenPos.x ) );
        int pixelY = static_cast<int>( glm::floor( screenPos.y ) );
        if( pixelX < vp.x || pixelX >= vp.x + vp.width || pixelY < vp.y || pixelY >= vp.y + vp.height )
            return;

        size_t fbIndex = frameBufferIndex( pixelX, pixelY, frameWidth );
        float depth = ndc.z;
        if( depth >= frameBuffers.depthBuffer[fbIndex] )
            return;

        PSInput psIn;
        psIn.color = vertex.color;
        psIn.barycentric = glm::vec3( 1.0f, 0.0f, 0.0f );
        psIn.depth = depth;

        shadeAndWritePixel( fbIndex, depth, psIn, ctx );
    }

    void Device::rasterizeLine( const VSOutput &v0, const VSOutput &v1, const ShaderContext &ctx )
    {
        Viewport vp = resolveViewport( rsStage.viewport, frameWidth, frameHeight );

        auto p0 = perspectiveDivide( v0 );
        auto p1 = perspectiveDivide( v1 );

        glm::vec2 s0 = ndcToViewport( p0, vp );
        glm::vec2 s1 = ndcToViewport( p1, vp );
        glm::vec2 segment = s1 - s0;
        float segmentLen2 = glm::dot( segment, segment );
        float halfWidth = clampLineWidth( rsStage.lineWidth ) * 0.5f;

        if( segmentLen2 == 0.0f )
        {
            rasterizePoint( v0, ctx );
            return;
        }

        RasterBounds bounds{ static_cast<int>( glm::floor( glm::min( s0.x, s1.x ) - halfWidth ) ),
                             static_cast<int>( glm::ceil( glm::max( s0.x, s1.x ) + halfWidth ) ),
                             static_cast<int>( glm::floor( glm::min( s0.y, s1.y ) - halfWidth ) ),
                             static_cast<int>( glm::ceil( glm::max( s0.y, s1.y ) + halfWidth ) ) };
        if( !clipBoundsToViewport( bounds, vp ) )
            return;

        float invW0 = 1.0f / v0.position.w;
        float invW1 = 1.0f / v1.position.w;

        for( int y = bounds.minY; y <= bounds.maxY; ++y )
        {
            for( int x = bounds.minX; x <= bounds.maxX; ++x )
            {
                glm::vec2 samplePos( static_cast<float>( x ) + 0.5f, static_cast<float>( y ) + 0.5f );
                float t = glm::dot( samplePos - s0, segment ) / segmentLen2;
                t = glm::clamp( t, 0.0f, 1.0f );

                glm::vec2 closestPoint = s0 + segment * t;
                float dist2 = glm::dot( samplePos - closestPoint, samplePos - closestPoint );
                if( dist2 > halfWidth * halfWidth )
                    continue;

                float w0 = 1.0f - t;
                float w1 = t;
                float denom = w0 * invW0 + w1 * invW1;
                if( denom <= 0.0f )
                    continue;

                float depth = ( w0 * p0.z * invW0 + w1 * p1.z * invW1 ) / denom;
                size_t fbIndex = frameBufferIndex( x, y, frameWidth );
                if( depth >= frameBuffers.depthBuffer[fbIndex] )
                    continue;

                PSInput psIn;
                glm::vec3 colorNum = w0 * v0.color * invW0 + w1 * v1.color * invW1;
                psIn.color = colorNum / denom;
                psIn.barycentric = glm::vec3( w0, w1, 0.0f );
                psIn.depth = depth;

                shadeAndWritePixel( fbIndex, depth, psIn, ctx );
            }
        }
    }

    void Device::rasterizeTriangle( const VSOutput &v0, const VSOutput &v1, const VSOutput &v2,
                                    const ShaderContext &ctx )
    {
        // Получаем viewport (если не задан, используем весь кадр)
        Viewport vp = resolveViewport( rsStage.viewport, frameWidth, frameHeight );

        // Clip to NDC space
        auto p0 = perspectiveDivide( v0 );
        auto p1 = perspectiveDivide( v1 );
        auto p2 = perspectiveDivide( v2 );

        // NDC to Screen space (viewport transform)
        glm::vec2 s0 = ndcToViewport( p0, vp );
        glm::vec2 s1 = ndcToViewport( p1, vp );
        glm::vec2 s2 = ndcToViewport( p2, vp );

        // Boundig box
        RasterBounds bounds{ static_cast<int>( glm::floor( glm::min( glm::min( s0.x, s1.x ), s2.x ) ) ),
                             static_cast<int>( glm::ceil( glm::max( glm::max( s0.x, s1.x ), s2.x ) ) ),
                             static_cast<int>( glm::floor( glm::min( glm::min( s0.y, s1.y ), s2.y ) ) ),
                             static_cast<int>( glm::ceil( glm::max( glm::max( s0.y, s1.y ), s2.y ) ) ) };
        if( !clipBoundsToViewport( bounds, vp ) )
            return;

        // Полная площадь треугольника
        float area = edgeFunction( s0, s1, s2 );
        if( area == 0.0f )
            return; // Вырожденный треугольник

        // RS: Отсечение задних граней (простая политика: area>0 считаем фронт-фейс)
        if( rsStage.cullBackface )
        {
            if( area < 0.0f )
                return;
        }

        // Растеризация внутри ограничивающего прямоугольника
        for( int y = bounds.minY; y <= bounds.maxY; ++y )
        {
            for( int x = bounds.minX; x <= bounds.maxX; ++x )
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
                    const float epsPixels = clampLineWidth( rsStage.lineWidth ) * 0.5f;
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

                    size_t fbIndex = frameBufferIndex( x, y, frameWidth );
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

                        shadeAndWritePixel( fbIndex, depth, psIn, ctx );
                    }
                }
            }
        }
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
    void Device::IAStage::setInputLayout( std::shared_ptr<InputLayout> layout )
    {
        inputLayout = std::move( layout );
    }

    // VSStage
    void Device::VSStage::setVertexShader( VertexShader shader )
    {
        vertexShader = std::move( shader );
    }
    void Device::VSStage::setConstantBuffer( size_t slot, std::shared_ptr<Buffer> buffer )
    {
        setConstantBufferSlot( constantBuffers, slot, std::move( buffer ) );
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
    void Device::RSStage::setLineWidth( float lw )
    {
        lineWidth = clampLineWidth( lw );
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
    void Device::PSStage::setConstantBuffer( size_t slot, std::shared_ptr<Buffer> buffer )
    {
        setConstantBufferSlot( constantBuffers, slot, std::move( buffer ) );
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
