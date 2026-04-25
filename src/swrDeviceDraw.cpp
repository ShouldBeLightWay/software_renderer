#include <assert.h>

#include "swrDevice.h"

namespace
{
    static inline size_t estimatePrimitiveCount( swr::PrimitiveTopology topology, size_t elementCount )
    {
        switch( topology )
        {
        case swr::PrimitiveTopology::PointList:
            return elementCount;

        case swr::PrimitiveTopology::LineList:
            return elementCount / 2;

        case swr::PrimitiveTopology::LineStrip:
            return elementCount > 1 ? elementCount - 1 : 0;

        case swr::PrimitiveTopology::TriangleList:
            return elementCount / 3;

        case swr::PrimitiveTopology::TriangleStrip:
        case swr::PrimitiveTopology::TriangleFan:
            return elementCount > 2 ? elementCount - 2 : 0;

        default:
            return 0;
        }
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

    void Device::drawLinear( const std::vector<AssembledPrimitive> &primitives, const ShaderContext &ctx,
                             const DrawDispatchState &dispatchState )
    {
        for( const AssembledPrimitive &primitive : primitives )
            rasterizePrimitive( primitive, ctx, dispatchState, nullptr );
    }

    void Device::draw( size_t vertexCount, size_t startVertexLocation )
    {
        DrawState drawState( vsStage.constantBuffers, psStage.constantBuffers );
        DrawDispatchState dispatchState;
        if( !prepareDrawState( drawState, dispatchState ) )
            return;

        assembledPrimitivesScratch.clear();
        assembledPrimitivesScratch.reserve( estimatePrimitiveCount( iaStage.primitiveTopology, vertexCount ) );

        emitNonIndexedPrimitives(
            drawState.vertexData, drawState.stride, startVertexLocation, vertexCount, drawState.inputLayout.get(),
            drawState.shaderContext, drawState.vertexShader, iaStage.primitiveTopology,
            [&]( const AssembledPrimitive &primitive ) { assembledPrimitivesScratch.push_back( primitive ); } );

        lastSubmittedPrimitiveCount = assembledPrimitivesScratch.size();

        if( dispatchState.tileRasterEnabled )
            drawTiled( assembledPrimitivesScratch, drawState.shaderContext, dispatchState );
        else
            drawLinear( assembledPrimitivesScratch, drawState.shaderContext, dispatchState );
    }

    void Device::drawIndexed( size_t indexCount, size_t startIndexLocation, size_t baseVertexLocation )
    {
        DrawState drawState( vsStage.constantBuffers, psStage.constantBuffers );
        DrawDispatchState dispatchState;
        if( !prepareDrawState( drawState, dispatchState ) )
            return;

        assembledPrimitivesScratch.clear();
        assembledPrimitivesScratch.reserve( estimatePrimitiveCount( iaStage.primitiveTopology, indexCount ) );

        auto ib = iaStage.indexBuffer;
        if( !ib )
        {
            assert( false && "Index buffer not set" );
            return;
        }

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
            [&]( const AssembledPrimitive &primitive ) { assembledPrimitivesScratch.push_back( primitive ); } );

        lastSubmittedPrimitiveCount = assembledPrimitivesScratch.size();

        if( dispatchState.tileRasterEnabled )
            drawTiled( assembledPrimitivesScratch, drawState.shaderContext, dispatchState );
        else
            drawLinear( assembledPrimitivesScratch, drawState.shaderContext, dispatchState );
    }

    void Device::rasterizePrimitive( const AssembledPrimitive &primitive, const ShaderContext &ctx,
                                     const DrawDispatchState &dispatchState, const RasterTileClipState *tileClip )
    {
        switch( primitive.kind )
        {
        case PrimitiveKind::Point:
            rasterizePoint( primitive.vertices[0], ctx, dispatchState, tileClip );
            break;

        case PrimitiveKind::Line:
            rasterizeLine( primitive.vertices[0], primitive.vertices[1], ctx, dispatchState, tileClip );
            break;

        case PrimitiveKind::Triangle:
            rasterizeTriangle( primitive.vertices[0], primitive.vertices[1], primitive.vertices[2], ctx, dispatchState,
                               tileClip );
            break;

        default:
            assert( false && "Unsupported primitive kind" );
            break;
        }
    }

} // namespace swr