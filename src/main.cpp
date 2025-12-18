#include <SDL3/SDL.h>
#include <iostream>

#include "IScene.h"
#include "SceneManager.h"
#include "TriangleScene.h"
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

    // Enable adaptive VSync if possible (fallback to normal vsync)
    if( !SDL_SetRenderVSync( renderer, SDL_RENDERER_VSYNC_ADAPTIVE ) )
    {
        // Fallback to standard vsync interval 1
        SDL_SetRenderVSync( renderer, 1 );
    }

    // Create texture for rendering (match renderer output size)
    int outW = 0, outH = 0;
    SDL_GetRenderOutputSize( renderer, &outW, &outH );
    if( outW == 0 || outH == 0 )
    {
        outW = 800;
        outH = 600;
    }
    SDL_Texture *texture =
        SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, outW, outH );
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
    std::shared_ptr<swr::Device> device = swr::Device::create( outW, outH );

    // Scene system setup
    SceneManager sceneManager;
    sceneManager.registerScene( "Triangle", []( std::shared_ptr<swr::Device> dev ) {
        return std::make_unique<TriangleScene>( std::move( dev ) );
    } );
    if( !sceneManager.setCurrentScene( "Triangle", device ) )
    {
        std::cerr << "Failed to create Triangle scene" << std::endl;
        SDL_DestroyTexture( texture );
        SDL_DestroyRenderer( renderer );
        SDL_DestroyWindow( window );
        SDL_Quit();
        return 1;
    }
    sceneManager.getCurrent()->init();

    // Main loop
    bool running = true;
    SDL_Event event;

    // Optionally, scene sets clear color inside init()

    // High-resolution timing setup
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 lastCounter = SDL_GetPerformanceCounter();

    while( running )
    {
        // Compute delta time in seconds
        Uint64 now = SDL_GetPerformanceCounter();
        double dtSec = static_cast<double>( now - lastCounter ) / static_cast<double>( perfFreq );
        lastCounter = now;
        // Clamp dt to avoid huge steps on hitches
        if( dtSec < 0.0 )
            dtSec = 0.0; // just in case
        if( dtSec > 0.1 )
            dtSec = 0.1; // cap ~100ms

        while( SDL_PollEvent( &event ) )
        {
            if( event.type == SDL_EVENT_QUIT )
            {
                running = false;
            }
            else if( event.type == SDL_EVENT_WINDOW_RESIZED )
            {
                // Query actual render output size (handles HiDPI)
                int newW = 0, newH = 0;
                SDL_GetRenderOutputSize( renderer, &newW, &newH );
                if( newW <= 0 || newH <= 0 )
                    continue;

                // Recreate texture
                SDL_DestroyTexture( texture );
                texture =
                    SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, newW, newH );
                if( !texture )
                {
                    std::cerr << "SDL_CreateTexture (resize) failed: " << SDL_GetError() << std::endl;
                    running = false;
                    continue;
                }

                // Resize device buffers and notify scene
                device->resize( static_cast<size_t>( newW ), static_cast<size_t>( newH ) );
                if( auto *scene = sceneManager.getCurrent() )
                    scene->onResize( newW, newH );
            }
            else if( event.type == SDL_EVENT_KEY_DOWN )
            {
                SDL_KeyboardEvent &ke = event.key;
                // Переключение сцен по стрелкам
                if( ke.key == SDLK_RIGHT )
                {
                    if( sceneManager.switchNext( device ) )
                    {
                        if( auto *scene = sceneManager.getCurrent() )
                        {
                            scene->init();
                            scene->onResize( static_cast<int>( device->deviceFrameWidth() ),
                                             static_cast<int>( device->deviceFrameHeight() ) );
                        }
                    }
                }
                else if( ke.key == SDLK_LEFT )
                {
                    if( sceneManager.switchPrev( device ) )
                    {
                        if( auto *scene = sceneManager.getCurrent() )
                        {
                            scene->init();
                            scene->onResize( static_cast<int>( device->deviceFrameWidth() ),
                                             static_cast<int>( device->deviceFrameHeight() ) );
                        }
                    }
                }
                else
                {
                    if( auto *scene = sceneManager.getCurrent() )
                        scene->handleKeyEvent( ke );
                }
            }
            else if( event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP )
            {
                SDL_MouseButtonEvent &mbe = event.button;
                if( auto *scene = sceneManager.getCurrent() )
                    scene->handleMouseBtnEvent( mbe );
            }
            else if( event.type == SDL_EVENT_MOUSE_MOTION )
            {
                SDL_MouseMotionEvent &mme = event.motion;
                if( auto *scene = sceneManager.getCurrent() )
                    scene->handleMouseMoveEvent( mme );
            }
        }

        // Clear device
        device->clear();
        // Prepare and render via current scene
        if( auto *scene = sceneManager.getCurrent() )
        {
            scene->prepareFrame( static_cast<float>( dtSec ) );
            scene->renderFrame();
            scene->endFrame();
        }

        // Present the rendered frame
        device->present( renderer, texture );

        // No SDL_Delay here; VSync will pace via SDL_RenderPresent
    }

    // Cleanup
    SDL_DestroyTexture( texture );
    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    SDL_Quit();

    return 0;
}
