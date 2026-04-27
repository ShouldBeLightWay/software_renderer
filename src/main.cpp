#include <SDL3/SDL.h>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "IScene.h"
#include "PrimitiveTopologyScene.h"
#include "SceneManager.h"
#include "TriangleScene.h"
#include "swrDevice.h"

namespace
{
    constexpr int kDefaultWindowWidth = 800;
    constexpr int kDefaultWindowHeight = 600;
    constexpr int kDefaultTileSize = 16;

    struct DebugOverlayState
    {
        bool tileRasterEnabled = false;
        int tileWidth = kDefaultTileSize;
        int tileHeight = kDefaultTileSize;
        int viewportWidth = 0;
        int viewportHeight = 0;
        int tilesX = 0;
        int tilesY = 0;
        size_t primitiveCount = 0;
        float frameTimeMs = 0.0f;
        float fps = 0.0f;
        std::string sceneName;
    };

    struct TextureLock
    {
        SDL_Texture *tex{ nullptr };
        void *pixels{ nullptr };
        int pitch{ 0 };
        bool ok{ false };

        explicit TextureLock( SDL_Texture *texture, const SDL_Rect *rect = nullptr ) : tex( texture )
        {
            ok = SDL_LockTexture( tex, rect, &pixels, &pitch );
        }

        ~TextureLock()
        {
            if( ok )
                SDL_UnlockTexture( tex );
        }

        TextureLock( const TextureLock & ) = delete;
        TextureLock &operator=( const TextureLock & ) = delete;
    };

    void destroySdlResources( SDL_Window *window, SDL_Renderer *renderer, SDL_Texture *texture )
    {
        SDL_DestroyTexture( texture );
        SDL_DestroyRenderer( renderer );
        SDL_DestroyWindow( window );
        SDL_Quit();
    }

    void enableVSync( SDL_Renderer *renderer )
    {
        if( !SDL_SetRenderVSync( renderer, SDL_RENDERER_VSYNC_ADAPTIVE ) )
            SDL_SetRenderVSync( renderer, 1 );
    }

    void queryRenderOutputSize( SDL_Renderer *renderer, int &outWidth, int &outHeight )
    {
        SDL_GetRenderOutputSize( renderer, &outWidth, &outHeight );
        if( outWidth > 0 && outHeight > 0 )
            return;

        outWidth = kDefaultWindowWidth;
        outHeight = kDefaultWindowHeight;
    }

    SDL_Texture *createStreamingTexture( SDL_Renderer *renderer, int width, int height )
    {
        SDL_Texture *texture =
            SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height );
        if( texture )
            SDL_SetTextureBlendMode( texture, SDL_BLENDMODE_NONE );
        return texture;
    }

    void presentColorBuffer( SDL_Renderer *renderer, SDL_Texture *texture, const std::vector<glm::vec4> &colorBuffer,
                             size_t width, size_t height )
    {
        assert( renderer != nullptr );
        assert( texture != nullptr );
        assert( width * height == colorBuffer.size() );

        static const SDL_PixelFormatDetails *pf = SDL_GetPixelFormatDetails( SDL_PIXELFORMAT_RGBA8888 );
        auto vec4ColorToRGBA8 = []( const glm::vec4 &color, const SDL_PixelFormatDetails *pfmt ) -> std::uint32_t {
            std::uint32_t r = static_cast<std::uint32_t>( glm::clamp( color.r, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t g = static_cast<std::uint32_t>( glm::clamp( color.g, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t b = static_cast<std::uint32_t>( glm::clamp( color.b, 0.0f, 1.0f ) * 255.0f );
            std::uint32_t a = static_cast<std::uint32_t>( glm::clamp( color.a, 0.0f, 1.0f ) * 255.0f );
            return ( ( r << pfmt->Rshift ) & pfmt->Rmask ) | ( ( g << pfmt->Gshift ) & pfmt->Gmask ) |
                   ( ( b << pfmt->Bshift ) & pfmt->Bmask ) | ( ( a << pfmt->Ashift ) & pfmt->Amask );
        };

        TextureLock lock( texture );
        if( !lock.ok )
        {
            std::cerr << "SDL_LockTexture failed: " << SDL_GetError() << std::endl;
            return;
        }

        assert( lock.pixels != nullptr );
        assert( lock.pitch >= static_cast<int>( width ) * 4 );

        auto *row = static_cast<std::uint8_t *>( lock.pixels );
        for( size_t y = 0; y < height; ++y )
        {
            auto *dst32 = reinterpret_cast<std::uint32_t *>( row );
            const size_t base = y * width;
            for( size_t x = 0; x < width; ++x )
                dst32[x] = vec4ColorToRGBA8( colorBuffer[base + x], pf );
            row += lock.pitch;
        }

        SDL_SetRenderViewport( renderer, nullptr );
        SDL_SetRenderScale( renderer, 1.0f, 1.0f );
        SDL_SetRenderDrawColor( renderer, 0, 0, 0, 255 );
        SDL_RenderClear( renderer );
        SDL_FRect dst{ 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) };
        SDL_RenderTexture( renderer, texture, nullptr, &dst );
    }

    void renderDebugOverlay( SDL_Renderer *renderer, const DebugOverlayState &overlayState )
    {
        std::string line1 = "FPS: " + std::to_string( static_cast<int>( overlayState.fps + 0.5f ) ) +
                            "  Frame: " + std::to_string( overlayState.frameTimeMs ) + " ms";
        std::string line2 = std::string( "Tile raster: " ) + ( overlayState.tileRasterEnabled ? "ON" : "OFF" ) +
                            "  Tile size: " + std::to_string( overlayState.tileWidth ) + "x" +
                            std::to_string( overlayState.tileHeight );
        std::string line3 = "Scene: " + overlayState.sceneName +
                            "  Viewport: " + std::to_string( overlayState.viewportWidth ) + "x" +
                            std::to_string( overlayState.viewportHeight );
        std::string line4 = "Primitives: " + std::to_string( overlayState.primitiveCount ) +
                            "  Tiles: " + std::to_string( overlayState.tilesX ) + "x" +
                            std::to_string( overlayState.tilesY );

        SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );
        SDL_SetRenderDrawColor( renderer, 0, 0, 0, 180 );
        SDL_FRect backdrop{ 8.0f, 8.0f, 430.0f, 74.0f };
        SDL_RenderFillRect( renderer, &backdrop );

        SDL_SetRenderDrawColor( renderer, 255, 255, 255, 255 );
        SDL_RenderDebugText( renderer, 14.0f, 14.0f, line1.c_str() );
        SDL_RenderDebugText( renderer, 14.0f, 30.0f, line2.c_str() );
        SDL_RenderDebugText( renderer, 14.0f, 46.0f, line3.c_str() );
        SDL_RenderDebugText( renderer, 14.0f, 62.0f, line4.c_str() );
    }

    swr::Device::PresentCallback makePresentCallback( SDL_Renderer *renderer, SDL_Texture *&texture,
                                                      const DebugOverlayState &overlayState )
    {
        return [renderer, &texture, &overlayState]( const std::vector<glm::vec4> &colorBuffer, size_t width,
                                                    size_t height ) {
            presentColorBuffer( renderer, texture, colorBuffer, width, height );
            renderDebugOverlay( renderer, overlayState );
            SDL_RenderPresent( renderer );
        };
    }

    bool initializeSceneManager( SceneManager &sceneManager, const std::shared_ptr<swr::Device> &device )
    {
        sceneManager.registerScene( "Primitive Topology", []( std::shared_ptr<swr::Device> dev ) {
            return std::make_unique<PrimitiveTopologyScene>( std::move( dev ) );
        } );
        sceneManager.registerScene( "Triangle", []( std::shared_ptr<swr::Device> dev ) {
            return std::make_unique<TriangleScene>( std::move( dev ) );
        } );

        if( !sceneManager.setCurrentScene( "Triangle", device ) )
            return false;

        sceneManager.getCurrent()->init();
        return true;
    }

    void notifyCurrentSceneResize( SceneManager &sceneManager, const std::shared_ptr<swr::Device> &device )
    {
        if( auto *scene = sceneManager.getCurrent() )
        {
            scene->onResize( static_cast<int>( device->deviceFrameWidth() ),
                             static_cast<int>( device->deviceFrameHeight() ) );
        }
    }

    bool handleResize( SDL_Renderer *renderer, SDL_Texture *&texture, const std::shared_ptr<swr::Device> &device,
                       SceneManager &sceneManager )
    {
        int newWidth = 0;
        int newHeight = 0;
        SDL_GetRenderOutputSize( renderer, &newWidth, &newHeight );
        if( newWidth <= 0 || newHeight <= 0 )
            return true;

        SDL_Texture *newTexture = createStreamingTexture( renderer, newWidth, newHeight );
        if( !newTexture )
        {
            std::cerr << "SDL_CreateTexture (resize) failed: " << SDL_GetError() << std::endl;
            return false;
        }

        SDL_DestroyTexture( texture );
        texture = newTexture;

        device->resize( static_cast<size_t>( newWidth ), static_cast<size_t>( newHeight ) );
        notifyCurrentSceneResize( sceneManager, device );
        return true;
    }

    void reinitializeCurrentScene( SceneManager &sceneManager, const std::shared_ptr<swr::Device> &device )
    {
        if( auto *scene = sceneManager.getCurrent() )
        {
            scene->init();
            notifyCurrentSceneResize( sceneManager, device );
        }
    }

    void printTileRasterState( const std::shared_ptr<swr::Device> &device, int tileWidth, int tileHeight )
    {
        std::cout << "Tile raster: " << ( device->isTileRasterEnabled() ? "ON" : "OFF" ) << ", tile size: " << tileWidth
                  << "x" << tileHeight << std::endl;
    }

    void handleKeyboardEvent( SceneManager &sceneManager, const std::shared_ptr<swr::Device> &device,
                              SDL_KeyboardEvent &keyboardEvent, DebugOverlayState &overlayState )
    {
        if( keyboardEvent.key == SDLK_RIGHT )
        {
            if( sceneManager.switchNext( device ) )
                reinitializeCurrentScene( sceneManager, device );
            return;
        }

        if( keyboardEvent.key == SDLK_LEFT )
        {
            if( sceneManager.switchPrev( device ) )
                reinitializeCurrentScene( sceneManager, device );
            return;
        }

        if( keyboardEvent.key == SDLK_T )
        {
            device->setTileRasterEnabled( !device->isTileRasterEnabled() );
            overlayState.tileRasterEnabled = device->isTileRasterEnabled();
            printTileRasterState( device, overlayState.tileWidth, overlayState.tileHeight );
            return;
        }

        if( keyboardEvent.key == SDLK_MINUS )
        {
            overlayState.tileWidth = std::max( 1, overlayState.tileWidth / 2 );
            overlayState.tileHeight = std::max( 1, overlayState.tileHeight / 2 );
            device->setTileSize( overlayState.tileWidth, overlayState.tileHeight );
            printTileRasterState( device, overlayState.tileWidth, overlayState.tileHeight );
            return;
        }

        if( keyboardEvent.key == SDLK_EQUALS || keyboardEvent.key == SDLK_PLUS )
        {
            overlayState.tileWidth *= 2;
            overlayState.tileHeight *= 2;
            device->setTileSize( overlayState.tileWidth, overlayState.tileHeight );
            printTileRasterState( device, overlayState.tileWidth, overlayState.tileHeight );
            return;
        }

        if( auto *scene = sceneManager.getCurrent() )
            scene->handleKeyEvent( keyboardEvent );
    }

    void renderFrame( const std::shared_ptr<swr::Device> &device, SceneManager &sceneManager, float dtSec )
    {
        device->clear();

        if( auto *scene = sceneManager.getCurrent() )
        {
            scene->prepareFrame( dtSec );
            scene->renderFrame();
            scene->endFrame();
        }

        device->present();
    }

    void updateDebugOverlayState( DebugOverlayState &overlayState, const std::shared_ptr<swr::Device> &device,
                                  const SceneManager &sceneManager )
    {
        overlayState.tileRasterEnabled = device->isTileRasterEnabled();
        overlayState.sceneName = sceneManager.getCurrentSceneName();
        overlayState.primitiveCount = device->lastFramePrimitiveCount();

        swr::Viewport viewport = device->activeViewport();
        overlayState.viewportWidth = viewport.width;
        overlayState.viewportHeight = viewport.height;
        overlayState.tilesX =
            viewport.width > 0 ? ( viewport.width + overlayState.tileWidth - 1 ) / overlayState.tileWidth : 0;
        overlayState.tilesY =
            viewport.height > 0 ? ( viewport.height + overlayState.tileHeight - 1 ) / overlayState.tileHeight : 0;
    }
} // unnamed namespace

int main( int argc, char *argv[] )
{
    // Initialize SDL
    if( !SDL_Init( SDL_INIT_VIDEO ) )
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create window
    SDL_Window *window =
        SDL_CreateWindow( "Software Renderer", kDefaultWindowWidth, kDefaultWindowHeight, SDL_WINDOW_RESIZABLE );

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

    enableVSync( renderer );

    int outW = 0;
    int outH = 0;
    queryRenderOutputSize( renderer, outW, outH );

    SDL_Texture *texture = createStreamingTexture( renderer, outW, outH );
    if( !texture )
    {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << std::endl;
        destroySdlResources( window, renderer, texture );
        return 1;
    }

    DebugOverlayState overlayState;
    std::shared_ptr<swr::Device> device =
        swr::Device::create( outW, outH, makePresentCallback( renderer, texture, overlayState ) );
    device->setTileSize( overlayState.tileWidth, overlayState.tileHeight );
    overlayState.tileRasterEnabled = device->isTileRasterEnabled();

    SceneManager sceneManager;
    if( !initializeSceneManager( sceneManager, device ) )
    {
        std::cerr << "Failed to create Triangle scene" << std::endl;
        destroySdlResources( window, renderer, texture );
        return 1;
    }

    bool running = true;
    SDL_Event event;

    std::cout << "Global controls: T toggle tiled renderer, - halve tile size, + double tile size" << std::endl;

    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 lastCounter = SDL_GetPerformanceCounter();

    while( running )
    {
        Uint64 now = SDL_GetPerformanceCounter();
        double dtSec = static_cast<double>( now - lastCounter ) / static_cast<double>( perfFreq );
        lastCounter = now;

        if( dtSec < 0.0 )
            dtSec = 0.0;
        if( dtSec > 0.1 )
            dtSec = 0.1;

        overlayState.frameTimeMs = static_cast<float>( dtSec * 1000.0 );
        overlayState.fps = dtSec > 0.0 ? static_cast<float>( 1.0 / dtSec ) : 0.0f;
        updateDebugOverlayState( overlayState, device, sceneManager );

        while( SDL_PollEvent( &event ) )
        {
            if( event.type == SDL_EVENT_QUIT )
            {
                running = false;
            }
            else if( event.type == SDL_EVENT_WINDOW_RESIZED )
            {
                running = handleResize( renderer, texture, device, sceneManager );
            }
            else if( event.type == SDL_EVENT_KEY_DOWN )
            {
                SDL_KeyboardEvent &keyboardEvent = event.key;
                handleKeyboardEvent( sceneManager, device, keyboardEvent, overlayState );
            }
            else if( event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP )
            {
                SDL_MouseButtonEvent &mouseButtonEvent = event.button;
                if( auto *scene = sceneManager.getCurrent() )
                    scene->handleMouseBtnEvent( mouseButtonEvent );
            }
            else if( event.type == SDL_EVENT_MOUSE_MOTION )
            {
                SDL_MouseMotionEvent &mouseMotionEvent = event.motion;
                if( auto *scene = sceneManager.getCurrent() )
                    scene->handleMouseMoveEvent( mouseMotionEvent );
            }
        }

        renderFrame( device, sceneManager, static_cast<float>( dtSec ) );
    }

    destroySdlResources( window, renderer, texture );
    return 0;
}
