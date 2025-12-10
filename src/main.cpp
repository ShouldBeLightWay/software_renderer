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

    // RS feature toggles
    bool wireframe = false;
    bool cullBackface = false;
    bool viewportEnabled = false;

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
            else if( event.type == SDL_EVENT_KEY_DOWN )
            {
                SDL_KeyboardEvent &ke = event.key;
                // Switch on key presses: W (wireframe), C (cull), V (viewport)
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
                        // Centered smaller viewport
                        swr::Viewport vp{ 200, 150, 400, 300, 0.0f, 1.0f };
                        device->RS().setViewport( vp );
                    }
                    else
                    {
                        // Full-frame viewport (width/height<=0 means use full frame in our implementation)
                        swr::Viewport vp{ 0, 0, 800, 600, 0.0f, 1.0f };
                        device->RS().setViewport( vp );
                    }
                    std::cout << "Viewport: " << ( viewportEnabled ? "SMALL" : "FULL" ) << std::endl;
                }
                else if( ke.key == SDLK_O )
                {
                    // Flip triangle winding by swapping two vertices
                    std::swap( vertices[1], vertices[2] );
                    vb->uploadData( vertices.data(), vertices.size() );
                    std::cout << "Winding flipped (O). With cull ON, triangle will toggle visibility." << std::endl;
                }
            }
        }

        // Clear device
        device->clear();
        // Here you would set up your pipeline stages and issue draw calls

        // Apply RS toggles every frame (in case of external changes)
        device->RS().setWireframe( wireframe );
        device->RS().setCullBackface( cullBackface );
        if( viewportEnabled )
        {
            swr::Viewport vp{ 200, 150, 400, 300, 0.0f, 1.0f };
            device->RS().setViewport( vp );
        }
        else
        {
            swr::Viewport vp{ 0, 0, 800, 600, 0.0f, 1.0f };
            device->RS().setViewport( vp );
        }

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
