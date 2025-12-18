#include "SceneManager.h"
#include "IScene.h"

void SceneManager::registerScene( const std::string &name, Factory f )
{
    registry[name] = std::move( f );
}

bool SceneManager::setCurrentScene( const std::string &name, std::shared_ptr<swr::Device> dev )
{
    auto it = registry.find( name );
    if( it == registry.end() )
    {
        return false;
    }
    current = it->second( std::move( dev ) );
    return current != nullptr;
}

IScene *SceneManager::getCurrent() const
{
    return current.get();
}
