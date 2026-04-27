#include <assert.h>

#include "swrDevice.h"

#if defined(SWR_HAS_TBB)
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#endif

namespace
{
    struct ScreenBounds
    {
        int minX = 0;
        int maxX = -1;
        int minY = 0;
        int maxY = -1;
    };

    struct TileBin
    {
        int minX = 0;
        int maxX = -1;
        int minY = 0;
        int maxY = -1;
        std::vector<size_t> primitiveIndices;
    };

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

    static inline bool clipScreenBounds( ScreenBounds &bounds, const swr::Viewport &vp )
    {
        bounds.minX = std::max( bounds.minX, vp.x );
        bounds.minY = std::max( bounds.minY, vp.y );
        bounds.maxX = std::min( bounds.maxX, vp.x + vp.width - 1 );
        bounds.maxY = std::min( bounds.maxY, vp.y + vp.height - 1 );
        return bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY;
    }

    static inline ScreenBounds computePrimitiveBounds( const swr::AssembledPrimitive &primitive,
                                                       const swr::Viewport &vp, float lineWidth )
    {
        ScreenBounds bounds;

        switch( primitive.kind )
        {
        case swr::PrimitiveKind::Point: {
            glm::vec3 ndc = perspectiveDivide( primitive.vertices[0] );
            glm::vec2 screen = ndcToViewport( ndc, vp );
            bounds.minX = static_cast<int>( glm::floor( screen.x ) );
            bounds.maxX = bounds.minX;
            bounds.minY = static_cast<int>( glm::floor( screen.y ) );
            bounds.maxY = bounds.minY;
            break;
        }

        case swr::PrimitiveKind::Line: {
            glm::vec3 p0 = perspectiveDivide( primitive.vertices[0] );
            glm::vec3 p1 = perspectiveDivide( primitive.vertices[1] );
            glm::vec2 s0 = ndcToViewport( p0, vp );
            glm::vec2 s1 = ndcToViewport( p1, vp );
            float halfWidth = std::max( 0.5f, lineWidth ) * 0.5f;
            bounds.minX = static_cast<int>( glm::floor( glm::min( s0.x, s1.x ) - halfWidth ) );
            bounds.maxX = static_cast<int>( glm::ceil( glm::max( s0.x, s1.x ) + halfWidth ) );
            bounds.minY = static_cast<int>( glm::floor( glm::min( s0.y, s1.y ) - halfWidth ) );
            bounds.maxY = static_cast<int>( glm::ceil( glm::max( s0.y, s1.y ) + halfWidth ) );
            break;
        }

        case swr::PrimitiveKind::Triangle: {
            glm::vec3 p0 = perspectiveDivide( primitive.vertices[0] );
            glm::vec3 p1 = perspectiveDivide( primitive.vertices[1] );
            glm::vec3 p2 = perspectiveDivide( primitive.vertices[2] );
            glm::vec2 s0 = ndcToViewport( p0, vp );
            glm::vec2 s1 = ndcToViewport( p1, vp );
            glm::vec2 s2 = ndcToViewport( p2, vp );
            bounds.minX = static_cast<int>( glm::floor( glm::min( glm::min( s0.x, s1.x ), s2.x ) ) );
            bounds.maxX = static_cast<int>( glm::ceil( glm::max( glm::max( s0.x, s1.x ), s2.x ) ) );
            bounds.minY = static_cast<int>( glm::floor( glm::min( glm::min( s0.y, s1.y ), s2.y ) ) );
            bounds.maxY = static_cast<int>( glm::ceil( glm::max( glm::max( s0.y, s1.y ), s2.y ) ) );
            break;
        }

        default:
            assert( false && "Unsupported primitive kind" );
            break;
        }

        return bounds;
    }

} // unnamed namespace

namespace swr
{

    void Device::processTileBin( const std::vector<AssembledPrimitive> &primitives, const ShaderContext &ctx,
                                 const DrawDispatchState &dispatchState, const RasterTileClipState &tileClip,
                                 const std::vector<size_t> &primitiveIndices )
    {
        for( size_t primitiveIndex : primitiveIndices )
            rasterizePrimitive( primitives[primitiveIndex], ctx, dispatchState, &tileClip );
    }

    void Device::drawTiled( const std::vector<AssembledPrimitive> &primitives, const ShaderContext &ctx,
                            const DrawDispatchState &dispatchState )
    {
        if( primitives.empty() )
            return;

        Viewport vp = dispatchState.viewport;
        int tileWidth = dispatchState.tileWidth;
        int tileHeight = dispatchState.tileHeight;
        int tilesX = ( vp.width + tileWidth - 1 ) / tileWidth;
        int tilesY = ( vp.height + tileHeight - 1 ) / tileHeight;
        if( tilesX <= 0 || tilesY <= 0 )
        {
            drawLinear( primitives, ctx, dispatchState );
            return;
        }

        std::vector<TileBin> bins( static_cast<size_t>( tilesX ) * static_cast<size_t>( tilesY ) );
        for( int tileY = 0; tileY < tilesY; ++tileY )
        {
            for( int tileX = 0; tileX < tilesX; ++tileX )
            {
                TileBin &bin =
                    bins[static_cast<size_t>( tileY ) * static_cast<size_t>( tilesX ) + static_cast<size_t>( tileX )];
                bin.minX = vp.x + tileX * tileWidth;
                bin.minY = vp.y + tileY * tileHeight;
                bin.maxX = std::min( bin.minX + tileWidth - 1, vp.x + vp.width - 1 );
                bin.maxY = std::min( bin.minY + tileHeight - 1, vp.y + vp.height - 1 );
            }
        }

        for( size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex )
        {
            ScreenBounds bounds = computePrimitiveBounds( primitives[primitiveIndex], vp, dispatchState.lineWidth );
            if( !clipScreenBounds( bounds, vp ) )
                continue;

            int firstTileX = ( bounds.minX - vp.x ) / tileWidth;
            int lastTileX = ( bounds.maxX - vp.x ) / tileWidth;
            int firstTileY = ( bounds.minY - vp.y ) / tileHeight;
            int lastTileY = ( bounds.maxY - vp.y ) / tileHeight;

            for( int tileY = firstTileY; tileY <= lastTileY; ++tileY )
            {
                for( int tileX = firstTileX; tileX <= lastTileX; ++tileX )
                {
                    bins[static_cast<size_t>( tileY ) * static_cast<size_t>( tilesX ) + static_cast<size_t>( tileX )]
                        .primitiveIndices.push_back( primitiveIndex );
                }
            }
        }

        auto processBin = [&]( const TileBin &bin ) {
            if( bin.primitiveIndices.empty() )
                return;

            RasterTileClipState tileClip;
            tileClip.enabled = true;
            tileClip.minX = bin.minX;
            tileClip.minY = bin.minY;
            tileClip.maxX = bin.maxX;
            tileClip.maxY = bin.maxY;
            processTileBin( primitives, ctx, dispatchState, tileClip, bin.primitiveIndices );
        };

#if defined(SWR_HAS_TBB)
        oneapi::tbb::parallel_for(
            oneapi::tbb::blocked_range<size_t>( 0, bins.size() ),
            [&]( const oneapi::tbb::blocked_range<size_t> &range ) {
                for( size_t binIndex = range.begin(); binIndex != range.end(); ++binIndex )
                    processBin( bins[binIndex] );
            } );
#else
        for( const TileBin &bin : bins )
            processBin( bin );
#endif
    }

} // namespace swr