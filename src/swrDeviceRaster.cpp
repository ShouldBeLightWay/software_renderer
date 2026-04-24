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

    static inline float edgeFunction( const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c )
    {
        return ( c.x - a.x ) * ( b.y - a.y ) - ( c.y - a.y ) * ( b.x - a.x );
    }

} // unnamed namespace

namespace swr
{

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
        Viewport vp = resolveViewport( rsStage.viewport, frameWidth, frameHeight );

        auto p0 = perspectiveDivide( v0 );
        auto p1 = perspectiveDivide( v1 );
        auto p2 = perspectiveDivide( v2 );

        glm::vec2 s0 = ndcToViewport( p0, vp );
        glm::vec2 s1 = ndcToViewport( p1, vp );
        glm::vec2 s2 = ndcToViewport( p2, vp );

        RasterBounds bounds{ static_cast<int>( glm::floor( glm::min( glm::min( s0.x, s1.x ), s2.x ) ) ),
                             static_cast<int>( glm::ceil( glm::max( glm::max( s0.x, s1.x ), s2.x ) ) ),
                             static_cast<int>( glm::floor( glm::min( glm::min( s0.y, s1.y ), s2.y ) ) ),
                             static_cast<int>( glm::ceil( glm::max( glm::max( s0.y, s1.y ), s2.y ) ) ) };
        if( !clipBoundsToViewport( bounds, vp ) )
            return;

        float area = edgeFunction( s0, s1, s2 );
        if( area == 0.0f )
            return;

        if( rsStage.cullBackface )
        {
            if( area < 0.0f )
                return;
        }

        for( int y = bounds.minY; y <= bounds.maxY; ++y )
        {
            for( int x = bounds.minX; x <= bounds.maxX; ++x )
            {
                glm::vec2 p( static_cast<float>( x ) + 0.5f, static_cast<float>( y ) + 0.5f );

                float w0 = edgeFunction( s1, s2, p );
                float w1 = edgeFunction( s2, s0, p );
                float w2 = edgeFunction( s0, s1, p );

                bool inside = ( area > 0.0f && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f ) ||
                              ( area < 0.0f && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f );

                bool onEdge = false;
                if( rsStage.wireframe )
                {
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

                    float invW0 = 1.0f / v0.position.w;
                    float invW1 = 1.0f / v1.position.w;
                    float invW2 = 1.0f / v2.position.w;
                    float denom = w0 * invW0 + w1 * invW1 + w2 * invW2;
                    if( denom <= 0.0f )
                        continue;

                    float depth = ( w0 * p0.z + w1 * p1.z + w2 * p2.z ) / denom;

                    size_t fbIndex = frameBufferIndex( x, y, frameWidth );
                    if( depth < frameBuffers.depthBuffer[fbIndex] )
                    {
                        PSInput psIn;
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

} // namespace swr