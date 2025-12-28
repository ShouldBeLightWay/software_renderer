#include "TriangleScene.h"

#include <algorithm>
#include <iostream>

#include "swrDevice.h"

TriangleScene::TriangleScene( std::shared_ptr<swr::Device> dev ) : IScene( std::move( dev ) )
{
}

void TriangleScene::init()
{
    // Set clear color to blue with full opacity
    device->OM().setClearColor( glm::vec4( 0.0f, 0.0f, 1.0f, 1.0f ) );

    // Setup vertices
    vertices = {
        { { 0.0f, 0.5f, 0.0f }, { 1, 0, 0 } },
        { { 0.5f, -0.5f, 0.0f }, { 0, 1, 0 } },
        { { -0.5f, -0.5f, 0.0f }, { 0, 0, 1 } },
    };

    vb = device->createBuffer( sizeof( swr::Vertex ), vertices.size(), swr::BufferFormat::Unknown );
    vb->uploadData( vertices.data(), vertices.size() );

    device->IA().setVertexBuffer( vb );
    device->IA().setPrimitiveTopology( swr::PrimitiveTopology::TriangleList );

    swr::VertexShader vs = [this]( const swr::Vertex &in ) -> swr::VSOutput {
        // Простая вращательная анимация вокруг оси Z, основанная на angle
        float c = std::cos( angle );
        float s = std::sin( angle );
        glm::vec3 p = in.position;
        glm::vec3 rotated{ p.x * c - p.y * s, p.x * s + p.y * c, p.z };
        swr::VSOutput out;
        out.position = glm::vec4( rotated, 1.0f );
        out.color = in.color;
        return out;
    };
    device->VS().setVertexShader( vs );

    swr::PixelShader ps = []( const swr::PSInput &in ) -> glm::vec4 { return glm::vec4( in.color, 1.0f ); };
    device->PS().setPixelShader( ps );

    // Default full viewport
    swr::Viewport vp{
        0,    0,   static_cast<int>( device->deviceFrameWidth() ), static_cast<int>( device->deviceFrameHeight() ),
        0.0f, 1.0f };
    device->RS().setViewport( vp );
}

void TriangleScene::prepareFrame( float dt )
{
    if( animate )
    {
        angle += angularSpeed * dt;
        // Нормализуем угол чтобы не рос бесконечно
        if( angle > 6.28318530718f )
            angle -= 6.28318530718f;
        if( angle < -6.28318530718f )
            angle += 6.28318530718f;
    }
    // Apply RS toggles every frame (in case of external changes)
    device->RS().setWireframe( wireframe );
    device->RS().setCullBackface( cullBackface );
    if( viewportEnabled )
    {
        // Процентный вьюпорт: центрированный, занимает 50% размера кадра
        float scale = 0.5f;
        int fw = static_cast<int>( device->deviceFrameWidth() );
        int fh = static_cast<int>( device->deviceFrameHeight() );
        int w = std::max( 1, static_cast<int>( fw * scale ) );
        int h = std::max( 1, static_cast<int>( fh * scale ) );
        int x = ( fw - w ) / 2;
        int y = ( fh - h ) / 2;
        swr::Viewport vp{ x, y, w, h, 0.0f, 1.0f };
        device->RS().setViewport( vp );
    }
    else
    {
        swr::Viewport vp{
            0,    0,   static_cast<int>( device->deviceFrameWidth() ), static_cast<int>( device->deviceFrameHeight() ),
            0.0f, 1.0f };
        device->RS().setViewport( vp );
    }
}

void TriangleScene::renderFrame()
{
    device->draw( vertices.size(), 0 );
}

void TriangleScene::endFrame()
{
    // Nothing for now
}

void TriangleScene::handleKeyEvent( SDL_KeyboardEvent &ke )
{
    // Switch on key presses: W (wireframe), C (cull), V (viewport), O (flip winding)
    if( ke.key == SDLK_W )
    {
        wireframe = !wireframe;
        device->RS().setWireframe( wireframe );
        std::cout << "Wireframe: " << ( wireframe ? "ON" : "OFF" ) << std::endl;
    }
    else if( ke.key == SDLK_C )
    {
        cullBackface = !cullBackface;
        device->RS().setCullBackface( cullBackface );
        std::cout << "Cull backface: " << ( cullBackface ? "ON" : "OFF" ) << std::endl;
    }
    else if( ke.key == SDLK_V )
    {
        viewportEnabled = !viewportEnabled;
        if( viewportEnabled )
        {
            // Процентный вьюпорт: центрированный 50% от размеров
            float scale = 0.5f;
            int fw = static_cast<int>( device->deviceFrameWidth() );
            int fh = static_cast<int>( device->deviceFrameHeight() );
            int w = std::max( 1, static_cast<int>( fw * scale ) );
            int h = std::max( 1, static_cast<int>( fh * scale ) );
            int x = ( fw - w ) / 2;
            int y = ( fh - h ) / 2;
            swr::Viewport vp{ x, y, w, h, 0.0f, 1.0f };
            device->RS().setViewport( vp );
        }
        else
        {
            swr::Viewport vp{ 0,
                              0,
                              static_cast<int>( device->deviceFrameWidth() ),
                              static_cast<int>( device->deviceFrameHeight() ),
                              0.0f,
                              1.0f };
            device->RS().setViewport( vp );
        }
        std::cout << "Viewport: " << ( viewportEnabled ? "SMALL" : "FULL" ) << std::endl;
    }
    else if( ke.key == SDLK_A )
    {
        animate = !animate;
        std::cout << "Animation: " << ( animate ? "ON" : "OFF" ) << std::endl;
    }
    else if( ke.key == SDLK_O )
    {
        std::swap( vertices[1], vertices[2] );
        vb->uploadData( vertices.data(), vertices.size() );
        std::cout << "Winding flipped (O). With cull ON, triangle will toggle visibility." << std::endl;
    }
}

void TriangleScene::onResize( int width, int height )
{
    // Обновим viewport под новый размер кадра
    if( viewportEnabled )
    {
        float scale = 0.5f;
        int w = std::max( 1, static_cast<int>( width * scale ) );
        int h = std::max( 1, static_cast<int>( height * scale ) );
        int x = ( width - w ) / 2;
        int y = ( height - h ) / 2;
        swr::Viewport vp{ x, y, w, h, 0.0f, 1.0f };
        device->RS().setViewport( vp );
    }
    else
    {
        swr::Viewport vp{ 0, 0, width, height, 0.0f, 1.0f };
        device->RS().setViewport( vp );
    }
}
