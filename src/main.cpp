#include <SDL3/SDL.h>
#include <iostream>

#include "swrDevice.h"

int main( int argc, char *argv[] )
{
    // Initialize SDL
    if( !SDL_Init( SDL_INIT_VIDEO ) )
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create window
    SDL_Window *window = SDL_CreateWindow( "Software Renderer", 800, 600, SDL_WINDOW_RESIZABLE );

    if( !window )
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Create renderer
    SDL_Renderer *renderer = SDL_CreateRenderer( window, nullptr );
    if( !renderer )
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow( window );
        SDL_Quit();
        return 1;
    }

    // Create texture for rendering (not used in this minimal example)
    SDL_Texture *texture =
        SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 800, 600 );
    if( !texture )
    {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer( renderer );
        SDL_DestroyWindow( window );
        SDL_Quit();
        return 1;
    }

    // Disable blending for the texture to avoid unexpected modulation
    SDL_SetTextureBlendMode( texture, SDL_BLENDMODE_NONE );

    // Create software rendering device
    std::shared_ptr<swr::Device> device = swr::Device::create( 800, 600 );

    std::vector<swr::Vertex> vertices = {
        { { 0.0f, 0.5f, 0.0f }, { 1, 0, 0 } },
        { { 0.5f, -0.5f, 0.0f }, { 0, 1, 0 } },
        { { -0.5f, -0.5f, 0.0f }, { 0, 0, 1 } },
    };

    auto vb = device->createBuffer( sizeof( swr::Vertex ), vertices.size() );
    vb->uploadData( vertices.data(), vertices.size() );

    device->IA().setVertexBuffer( vb );
    device->IA().setPrimitiveTopology( swr::PrimitiveTopology::TriangleList );

    swr::VertexShader vs = []( const swr::Vertex &in ) -> swr::VSOutput {
        swr::VSOutput out;
        out.position = glm::vec4( in.position, 1.0f ); // Прямая передача позиции
        out.color = in.color;                          // Прямая передача цвета
        return out;
    };

    device->VS().setVertexShader( vs );

    swr::PixelShader ps = []( const swr::PSInput &in ) -> glm::vec4 {
        return glm::vec4( in.color, 1.0f ); // Выводим цвет вершины с альфой 1.0
    };

    device->PS().setPixelShader( ps );

    // Main loop
    bool running = true;
    SDL_Event event;

    // Set clear color to blue with full opacity
    device->OM().setClearColor( glm::vec4( 0.0f, 0.0f, 1.0f, 1.0f ) );

    while( running )
    {
        while( SDL_PollEvent( &event ) )
        {
            if( event.type == SDL_EVENT_QUIT )
            {
                running = false;
            }
        }

        // Clear device
        device->clear();
        // Here you would set up your pipeline stages and issue draw calls

        device->draw( vertices.size(), 0 );

        // Present the rendered frame
        device->present( renderer, texture );

        // Keep the window responsive
        SDL_Delay( 16 ); // ~60 FPS
    }

    // Cleanup
    SDL_DestroyTexture( texture );
    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();

    return 0;
}
