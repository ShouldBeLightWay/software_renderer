#include "PrimitiveTopologyScene.h"

#include <algorithm>
#include <iostream>

#include "swrDevice.h"

PrimitiveTopologyScene::PrimitiveTopologyScene( std::shared_ptr<swr::Device> dev ) : IScene( std::move( dev ) )
{
}

void PrimitiveTopologyScene::init()
{
    device->OM().setClearColor( glm::vec4( 0.08f, 0.08f, 0.1f, 1.0f ) );
    device->RS().setCullBackface( false );
    device->RS().setLineWidth( lineWidth );
    device->RS().setWireframe( false );

    auto makePointCloudPreset = []() {
        std::vector<VertexPC> vertices;
        vertices.reserve( 54 );

        const std::vector<glm::vec2> cloudPattern = {
            { -0.10f, 0.18f },  { -0.04f, 0.11f },  { 0.03f, 0.15f },   { 0.10f, 0.08f },  { -0.12f, 0.02f },
            { -0.02f, 0.00f },  { 0.09f, -0.02f },  { -0.07f, -0.08f }, { 0.00f, -0.10f }, { 0.12f, -0.11f },
            { -0.10f, -0.18f }, { -0.01f, -0.20f }, { 0.08f, -0.16f },  { -0.14f, 0.10f }, { 0.14f, 0.16f },
            { -0.16f, -0.02f }, { 0.16f, 0.01f },   { 0.02f, 0.22f },
        };

        const std::vector<glm::vec3> cloudColors = {
            { 1.0f, 0.25f, 0.25f }, { 1.0f, 0.55f, 0.2f }, { 1.0f, 0.85f, 0.2f }, { 0.6f, 1.0f, 0.25f },
            { 0.25f, 1.0f, 0.45f }, { 0.2f, 0.95f, 0.8f }, { 0.2f, 0.75f, 1.0f }, { 0.45f, 0.55f, 1.0f },
            { 0.8f, 0.35f, 1.0f },  { 1.0f, 0.35f, 0.8f },
        };

        const std::vector<glm::vec3> cloudCenters = {
            { -0.62f, 0.42f, 0.18f },
            { 0.08f, -0.02f, 0.02f },
            { 0.62f, -0.36f, -0.14f },
        };

        for( size_t cloudIndex = 0; cloudIndex < cloudCenters.size(); ++cloudIndex )
        {
            const glm::vec3 &center = cloudCenters[cloudIndex];
            for( size_t i = 0; i < cloudPattern.size(); ++i )
            {
                const glm::vec2 &offset = cloudPattern[i];
                const glm::vec3 &color = cloudColors[( i + cloudIndex * 3 ) % cloudColors.size()];
                vertices.push_back( { { center.x + offset.x, center.y + offset.y, center.z }, color } );
            }
        }

        return vertices;
    };

    swr::InputLayoutDesc layoutDesc;
    layoutDesc.elements = {
        { swr::Semantic::POSITION0, swr::InputFormat::R32G32B32_FLOAT, offsetof( VertexPC, position ) },
        { swr::Semantic::COLOR0, swr::InputFormat::R32G32B32_FLOAT, offsetof( VertexPC, color ) },
    };
    layoutDesc.stride = sizeof( VertexPC );
    inputLayout = device->createInputLayout( layoutDesc );

    vb = device->createBuffer( sizeof( VertexPC ), 64, swr::BufferFormat::Unknown );

    swr::VertexShader vs = []( const swr::VertexInputView &input, const swr::ShaderContext &ctx ) -> swr::VSOutput {
        (void)ctx;

        swr::VSOutput out;
        out.position = glm::vec4( input.readFloat3( swr::Semantic::POSITION0 ), 1.0f );
        out.color = input.readFloat3( swr::Semantic::COLOR0 );
        return out;
    };
    device->VS().setVertexShader( vs );

    swr::PixelShader ps = []( const swr::PSInput &in, const swr::ShaderContext &ctx ) -> glm::vec4 {
        (void)ctx;
        return glm::vec4( in.color, 1.0f );
    };
    device->PS().setPixelShader( ps );

    device->IA().setVertexBuffer( vb );
    device->IA().setInputLayout( inputLayout );

    presets = {
        { "PointList", swr::PrimitiveTopology::PointList, makePointCloudPreset() },
        { "LineList",
          swr::PrimitiveTopology::LineList,
          { { { -0.8f, 0.6f, 0.2f }, { 1.0f, 0.2f, 0.2f } },
            { { -0.2f, 0.2f, 0.2f }, { 1.0f, 0.8f, 0.2f } },
            { { -0.75f, -0.45f, 0.0f }, { 0.2f, 1.0f, 0.4f } },
            { { 0.0f, -0.25f, 0.0f }, { 0.2f, 0.9f, 1.0f } },
            { { 0.15f, 0.7f, -0.2f }, { 0.8f, 0.2f, 1.0f } },
            { { 0.85f, -0.55f, -0.2f }, { 1.0f, 0.3f, 0.8f } } } },
        { "LineStrip",
          swr::PrimitiveTopology::LineStrip,
          { { { -0.85f, 0.45f, 0.2f }, { 1.0f, 0.25f, 0.25f } },
            { { -0.45f, -0.1f, 0.1f }, { 1.0f, 0.75f, 0.2f } },
            { { -0.1f, 0.55f, 0.0f }, { 0.5f, 1.0f, 0.2f } },
            { { 0.3f, -0.35f, -0.1f }, { 0.2f, 0.8f, 1.0f } },
            { { 0.8f, 0.2f, -0.2f }, { 0.8f, 0.3f, 1.0f } } } },
        { "TriangleStrip",
          swr::PrimitiveTopology::TriangleStrip,
          { { { -0.8f, -0.4f, 0.2f }, { 1.0f, 0.15f, 0.15f } },
            { { -0.75f, 0.45f, 0.2f }, { 1.0f, 0.6f, 0.15f } },
            { { -0.2f, -0.55f, 0.1f }, { 0.8f, 1.0f, 0.2f } },
            { { -0.05f, 0.55f, 0.0f }, { 0.2f, 1.0f, 0.45f } },
            { { 0.5f, -0.35f, -0.1f }, { 0.2f, 0.8f, 1.0f } },
            { { 0.7f, 0.35f, -0.2f }, { 0.8f, 0.3f, 1.0f } } } },
        { "TriangleFan",
          swr::PrimitiveTopology::TriangleFan,
          { { { 0.0f, 0.0f, 0.15f }, { 1.0f, 1.0f, 1.0f } },
            { { 0.0f, 0.72f, 0.1f }, { 1.0f, 0.25f, 0.25f } },
            { { 0.68f, 0.26f, 0.0f }, { 1.0f, 0.75f, 0.2f } },
            { { 0.42f, -0.62f, -0.1f }, { 0.25f, 1.0f, 0.45f } },
            { { -0.42f, -0.62f, -0.15f }, { 0.2f, 0.75f, 1.0f } },
            { { -0.68f, 0.26f, -0.05f }, { 0.85f, 0.3f, 1.0f } },
            { { 0.0f, 0.72f, 0.1f }, { 1.0f, 0.25f, 0.25f } } } },
    };

    applyPreset( currentPreset );
    setViewport( static_cast<int>( device->deviceFrameWidth() ), static_cast<int>( device->deviceFrameHeight() ) );

    std::cout << "PrimitiveTopology scene: 1 PointList, 2 LineList, 3 LineStrip, 4 TriangleStrip, 5 TriangleFan, Space "
                 "next preset, [/] line width"
              << std::endl;
}

void PrimitiveTopologyScene::renderFrame()
{
    device->draw( drawVertexCount, 0 );
}

void PrimitiveTopologyScene::handleKeyEvent( SDL_KeyboardEvent &ke )
{
    if( presets.empty() )
        return;

    if( ke.key == SDLK_1 )
        applyPreset( 0 );
    else if( ke.key == SDLK_2 && presets.size() > 1 )
        applyPreset( 1 );
    else if( ke.key == SDLK_3 && presets.size() > 2 )
        applyPreset( 2 );
    else if( ke.key == SDLK_4 && presets.size() > 3 )
        applyPreset( 3 );
    else if( ke.key == SDLK_5 && presets.size() > 4 )
        applyPreset( 4 );
    else if( ke.key == SDLK_SPACE )
        applyPreset( ( currentPreset + 1 ) % presets.size() );
    else if( ke.key == SDLK_LEFTBRACKET )
    {
        lineWidth = std::max( 0.5f, lineWidth - 0.5f );
        device->RS().setLineWidth( lineWidth );
        std::cout << "Line width: " << lineWidth;
        if( presetUsesLineWidth( currentPreset ) )
            std::cout << " px";
        else
            std::cout << " px (visible on LineList/LineStrip)";
        std::cout << std::endl;
    }
    else if( ke.key == SDLK_RIGHTBRACKET )
    {
        lineWidth += 0.5f;
        device->RS().setLineWidth( lineWidth );
        std::cout << "Line width: " << lineWidth;
        if( presetUsesLineWidth( currentPreset ) )
            std::cout << " px";
        else
            std::cout << " px (visible on LineList/LineStrip)";
        std::cout << std::endl;
    }
}

void PrimitiveTopologyScene::onResize( int width, int height )
{
    setViewport( width, height );
}

void PrimitiveTopologyScene::setViewport( int width, int height )
{
    device->RS().setViewport( swr::Viewport{ 0, 0, width, height, 0.0f, 1.0f } );
}

void PrimitiveTopologyScene::applyPreset( size_t presetIndex )
{
    if( presetIndex >= presets.size() )
        return;

    currentPreset = presetIndex;
    const TopologyPreset &preset = presets[currentPreset];

    vb->uploadData( preset.vertices.data(), preset.vertices.size() );
    drawVertexCount = preset.vertices.size();
    device->IA().setPrimitiveTopology( preset.topology );
    device->RS().setLineWidth( lineWidth );

    std::cout << "Primitive topology: " << preset.name << ", vertices: " << drawVertexCount << std::endl;
}

bool PrimitiveTopologyScene::presetUsesLineWidth( size_t presetIndex ) const
{
    if( presetIndex >= presets.size() )
        return false;

    swr::PrimitiveTopology topology = presets[presetIndex].topology;
    return topology == swr::PrimitiveTopology::LineList || topology == swr::PrimitiveTopology::LineStrip;
}