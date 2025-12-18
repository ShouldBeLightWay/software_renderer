#include "SceneManager.h"
#include "IScene.h"

void SceneManager::registerScene( const std::string &name, Factory f )
{
    auto it = registry.find( name );
    registry[name] = std::move( f );
    if( it == registry.end() )
    {
        order.push_back( name );
        if( currentIndex == -1 )
            currentIndex = 0;
    }
}

bool SceneManager::setCurrentScene( const std::string &name, std::shared_ptr<swr::Device> dev )
{
    auto it = registry.find( name );
    if( it == registry.end() )
    {
        return false;
    }
    // update index
    for( size_t i = 0; i < order.size(); ++i )
    {
        if( order[i] == name )
        {
            currentIndex = static_cast<int>( i );
            break;
        }
    }
    current = it->second( std::move( dev ) );
    return current != nullptr;
}

IScene *SceneManager::getCurrent() const
{
    return current.get();
}

bool SceneManager::switchNext( std::shared_ptr<swr::Device> dev )
{
    if( order.empty() )
        return false;
    int next = currentIndex;
    if( next == -1 )
        next = 0;
    else
        next = ( next + 1 ) % static_cast<int>( order.size() );
    return setCurrentScene( order[next], std::move( dev ) );
}

bool SceneManager::switchPrev( std::shared_ptr<swr::Device> dev )
{
    if( order.empty() )
        return false;
    int prev = currentIndex;
    if( prev == -1 )
        prev = 0;
    else
        prev = ( prev - 1 + static_cast<int>( order.size() ) ) % static_cast<int>( order.size() );
    return setCurrentScene( order[prev], std::move( dev ) );
}
