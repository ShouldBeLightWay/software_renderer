#include <SDL3/SDL.h>
#include <iostream>

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

    // Main loop
    bool running = true;
    SDL_Event event;

    while( running )
    {
        while( SDL_PollEvent( &event ) )
        {
            if( event.type == SDL_EVENT_QUIT )
            {
                running = false;
            }
        }

        // Keep the window responsive
        SDL_Delay( 16 ); // ~60 FPS
    }

    // Cleanup
    SDL_DestroyWindow( window );
    SDL_Quit();

    return 0;
}
