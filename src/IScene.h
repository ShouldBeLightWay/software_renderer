#pragma once

#include <SDL3/SDL.h>
#include <memory>

#include "swrDevice.h"

class IScene
{
  protected:
    std::shared_ptr<swr::Device> device;

  public:
    explicit IScene( std::shared_ptr<swr::Device> dev ) : device( std::move( dev ) )
    {
    }
    virtual ~IScene() = default;

    // Lifecycle
    virtual void init()
    {
    }
    virtual void prepareFrame( float dt )
    {
    }
    virtual void renderFrame() = 0;
    virtual void endFrame()
    {
    }

    // Event handlers
    virtual void handleKeyEvent( SDL_KeyboardEvent &ke )
    {
    }
    virtual void handleMouseBtnEvent( SDL_MouseButtonEvent &mbe )
    {
    }
    virtual void handleMouseMoveEvent( SDL_MouseMotionEvent &mme )
    {
    }
    // Resize notification (framebuffer size in pixels)
    virtual void onResize( int /*width*/, int /*height*/ )
    {
    }

    // Accessor if needed
    std::shared_ptr<swr::Device> getDevice() const
    {
        return device;
    }
};
