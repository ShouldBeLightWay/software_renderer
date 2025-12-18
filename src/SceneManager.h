#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "swrDevice.h"
class IScene;

class SceneManager
{
  public:
    using Factory = std::function<std::unique_ptr<IScene>( std::shared_ptr<swr::Device> )>;

    void registerScene( const std::string &name, Factory f );
    bool setCurrentScene( const std::string &name, std::shared_ptr<swr::Device> dev );
    IScene *getCurrent() const;

  private:
    std::unordered_map<std::string, Factory> registry;
    std::unique_ptr<IScene> current;
};
