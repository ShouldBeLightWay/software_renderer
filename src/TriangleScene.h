#pragma once

#include <memory>
#include <vector>

#include "IScene.h"

class TriangleScene : public IScene
{
  public:
    explicit TriangleScene( std::shared_ptr<swr::Device> dev );
    ~TriangleScene() override = default;

    void init() override;
    void prepareFrame( float dt ) override;
    void renderFrame() override;
    void endFrame() override;

    void handleKeyEvent( SDL_KeyboardEvent &ke ) override;
    void onResize( int width, int height ) override;

  private:
    bool wireframe = false;
    bool cullBackface = false;
    bool viewportEnabled = false;

    std::vector<swr::Vertex> vertices;
    std::shared_ptr<swr::Buffer> vb;
};
