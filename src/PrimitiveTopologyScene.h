#pragma once

#include <memory>
#include <vector>

#include "IScene.h"

class PrimitiveTopologyScene : public IScene
{
  public:
    explicit PrimitiveTopologyScene( std::shared_ptr<swr::Device> dev );
    ~PrimitiveTopologyScene() override = default;

    void init() override;
    void renderFrame() override;
    void handleKeyEvent( SDL_KeyboardEvent &ke ) override;
    void onResize( int width, int height ) override;

  private:
    struct VertexPC
    {
        glm::vec3 position;
        glm::vec3 color;
    };

    struct TopologyPreset
    {
        const char *name;
        swr::PrimitiveTopology topology;
        std::vector<VertexPC> vertices;
    };

    void setViewport( int width, int height );
    void applyPreset( size_t presetIndex );
    bool presetUsesLineWidth( size_t presetIndex ) const;

    std::vector<TopologyPreset> presets;
    size_t currentPreset = 0;
    size_t drawVertexCount = 0;
    float lineWidth = 1.0f;

    std::shared_ptr<swr::Buffer> vb;
    std::shared_ptr<swr::InputLayout> inputLayout;
};