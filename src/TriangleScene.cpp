#include "TriangleScene.h"

#include <algorithm>
#include <iostream>

#include "swrDevice.h"

// Local vertex structure for this scene
struct VertexPC
{
    glm::vec3 position;
    glm::vec3 color;
};

// Constant buffer structure
struct CBScene
{
    float angle;
    float padding[3]; // For alignment
};

TriangleScene::TriangleScene( std::shared_ptr<swr::Device> dev ) : IScene( std::move( dev ) )
{
}

void TriangleScene::init()
{
    // Set clear color to blue with full opacity
    device->OM().setClearColor( glm::vec4( 0.0f, 0.0f, 1.0f, 1.0f ) );

    // Setup vertices using local VertexPC structure
    std::vector<VertexPC> vertices = {
        { { 0.0f, 0.5f, 0.0f }, { 1, 0, 0 } },
        { { 0.5f, -0.5f, 0.0f }, { 0, 1, 0 } },
        { { -0.5f, -0.5f, 0.0f }, { 0, 0, 1 } },
    };

    // Create vertex buffer
    vb = device->createBuffer( sizeof( VertexPC ), vertices.size(), swr::BufferFormat::Unknown );
    vb->uploadData( vertices.data(), vertices.size() );

    // Create input layout describing how to interpret vertex data
    swr::InputLayoutDesc layoutDesc;
    layoutDesc.elements = {
        { swr::Semantic::POSITION0, swr::InputFormat::R32G32B32_FLOAT, offsetof( VertexPC, position ) },
        { swr::Semantic::COLOR0, swr::InputFormat::R32G32B32_FLOAT, offsetof( VertexPC, color ) },
    };
    layoutDesc.stride = sizeof( VertexPC );
    inputLayout = device->createInputLayout( layoutDesc );

    // Create constant buffer for scene parameters
    constantBuffer = device->createBuffer( sizeof( CBScene ), 1, swr::BufferFormat::Unknown );
    CBScene cbData{ angle, { 0.0f, 0.0f, 0.0f } };
    constantBuffer->uploadData( &cbData, 1 );

    // Set IA stage
    device->IA().setVertexBuffer( vb );
    device->IA().setInputLayout( inputLayout );
    device->IA().setPrimitiveTopology( swr::PrimitiveTopology::TriangleList );

    // Bind constant buffer to VS slot 0
    device->VS().setConstantBuffer( 0, constantBuffer );

    // Define vertex shader that reads angle from constant buffer
    swr::VertexShader vs = []( const swr::VertexInputView &input, const swr::ShaderContext &ctx ) -> swr::VSOutput {
        // Read angle from constant buffer slot 0
        const CBScene *cb = ctx.vsCB<CBScene>( 0 );
        float angle = cb ? cb->angle : 0.0f;

        // Read vertex attributes by semantic
        glm::vec3 position = input.readFloat3( swr::Semantic::POSITION0 );
        glm::vec3 color = input.readFloat3( swr::Semantic::COLOR0 );

        // Простая вращательная анимация вокруг оси Z, основанная на angle
        float c = std::cos( angle );
        float s = std::sin( angle );
        glm::vec3 rotated{ position.x * c - position.y * s, position.x * s + position.y * c, position.z };

        swr::VSOutput out;
        out.position = glm::vec4( rotated, 1.0f );
        out.color = color;
        return out;
    };
    device->VS().setVertexShader( vs );

    // Define pixel shader with ShaderContext parameter
    swr::PixelShader ps = []( const swr::PSInput &in, const swr::ShaderContext &ctx ) -> glm::vec4 {
        return glm::vec4( in.color, 1.0f );
    };
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

        // Update constant buffer with new angle
        CBScene cbData{ angle, { 0.0f, 0.0f, 0.0f } };
        constantBuffer->uploadData( &cbData, 1 );
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
    device->draw( 3, 0 ); // Draw 3 vertices starting at index 0
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
        // Flip winding by swapping vertices 1 and 2
        std::vector<VertexPC> vertices( 3 );
        const VertexPC *vbData = static_cast<const VertexPC *>( vb->data() );
        vertices[0] = vbData[0];
        vertices[1] = vbData[2]; // Swap
        vertices[2] = vbData[1]; // Swap
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
