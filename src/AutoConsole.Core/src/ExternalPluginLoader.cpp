#include "AutoConsole/Core/ExternalPluginLoader.h"

#include <filesystem>
#include <iostream>
#include <sstream>

#include <Windows.h>

#include "AutoConsole/Abstractions/ExternalPluginApi.h"
#include "AutoConsole/Abstractions/PluginMetadata.h"
#include "AutoConsole/Core/PluginHost.h"

namespace
{
    std::string format_last_error(const std::string& prefix)
    {
        return prefix + " (error=" + std::to_string(GetLastError()) + ")";
    }

    bool is_metadata_valid(const AutoConsole::Abstractions::PluginMetadata& metadata, std::string& errorMessage)
    {
        if (metadata.id.empty())
        {
            errorMessage = "metadata id is empty";
            return false;
        }
        if (metadata.displayName.empty())
        {
            errorMessage = "metadata displayName is empty";
            return false;
        }
        if (metadata.version.empty())
        {
            errorMessage = "metadata version is empty";
            return false;
        }
        if (metadata.apiVersion != AutoConsole::Abstractions::PluginApiVersion)
        {
            errorMessage = "metadata apiVersion mismatch";
            return false;
        }
        if (metadata.author.empty())
        {
            errorMessage = "metadata author is empty";
            return false;
        }
        if (metadata.description.empty())
        {
            errorMessage = "metadata description is empty";
            return false;
        }
        if (metadata.capabilities.empty())
        {
            errorMessage = "metadata capabilities is empty";
            return false;
        }
        return true;
    }
}

namespace AutoConsole::Core
{
    std::vector<std::string> ExternalPluginLoader::load_plugins(const std::string& directory, PluginHost& pluginHost) const
    {
        std::vector<std::string> errors;

        const std::filesystem::path baseDir(directory);
        if (!std::filesystem::exists(baseDir))
        {
            return errors;
        }

        try
        {
            for (const auto& entry : std::filesystem::directory_iterator(baseDir))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }

                const auto& path = entry.path();
                if (path.extension() != ".dll")
                {
                    continue;
                }

                const std::string pathString = path.string();
                std::cout << "[plugin-loader] loading dll: " << pathString << "\n";
                HMODULE module = LoadLibraryA(pathString.c_str());
                if (!module)
                {
                    errors.push_back(format_last_error("failed to load " + pathString));
                    continue;
                }

                auto createFn = reinterpret_cast<AutoConsole::Abstractions::CreatePluginFn>(
                    GetProcAddress(module, "CreatePlugin"));
                auto destroyFn = reinterpret_cast<AutoConsole::Abstractions::DestroyPluginFn>(
                    GetProcAddress(module, "DestroyPlugin"));
                auto getVersionFn = reinterpret_cast<AutoConsole::Abstractions::GetPluginApiVersionFn>(
                    GetProcAddress(module, "GetPluginApiVersion"));

                if (!createFn || !destroyFn)
                {
                    errors.push_back("missing required exports in " + pathString);
                    FreeLibrary(module);
                    continue;
                }

                if (getVersionFn)
                {
                    const int version = getVersionFn();
                    if (version != AutoConsole::Abstractions::PluginApiVersion)
                    {
                        errors.push_back("apiVersion mismatch in " + pathString);
                        FreeLibrary(module);
                        continue;
                    }
                }

                AutoConsole::Abstractions::IPlugin* rawPlugin = nullptr;
                try
                {
                    rawPlugin = createFn();
                }
                catch (const std::exception& ex)
                {
                    errors.push_back("CreatePlugin threw: " + std::string(ex.what()));
                    FreeLibrary(module);
                    continue;
                }
                catch (...)
                {
                    errors.push_back("CreatePlugin threw unknown error");
                    FreeLibrary(module);
                    continue;
                }

                if (!rawPlugin)
                {
                    errors.push_back("CreatePlugin returned null for " + pathString);
                    FreeLibrary(module);
                    continue;
                }

                std::string metadataError;
                AutoConsole::Abstractions::PluginMetadata metadata{};
                try
                {
                    metadata = rawPlugin->metadata();
                }
                catch (const std::exception& ex)
                {
                    metadataError = "metadata threw: " + std::string(ex.what());
                }
                catch (...)
                {
                    metadataError = "metadata threw unknown error";
                }

                if (!metadataError.empty() || !is_metadata_valid(metadata, metadataError))
                {
                    if (metadataError.empty())
                    {
                        metadataError = "invalid metadata";
                    }
                    errors.push_back(pathString + " rejected: " + metadataError);
                    try
                    {
                        destroyFn(rawPlugin);
                    }
                    catch (...)
                    {
                        errors.push_back(pathString + " rejected: DestroyPlugin threw");
                    }
                    FreeLibrary(module);
                    continue;
                }

                auto deleter = [destroyFn, module](AutoConsole::Abstractions::IPlugin* plugin)
                {
                    std::cout << "[plugin-loader] destroying plugin instance\n";
                    try
                    {
                        destroyFn(plugin);
                    }
                    catch (const std::exception& ex)
                    {
                        std::cout << "[plugin-loader] DestroyPlugin threw: " << ex.what() << "\n";
                    }
                    catch (...)
                    {
                        std::cout << "[plugin-loader] DestroyPlugin threw unknown error\n";
                    }
                    std::cout << "[plugin-loader] unloading dll\n";
                    FreeLibrary(module);
                };

                std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin(rawPlugin, deleter);
                std::string registerError;
                if (!pluginHost.register_plugin(plugin, PluginSource::External, registerError))
                {
                    errors.push_back(pathString + " rejected: " + registerError);
                    plugin.reset();
                    continue;
                }

                std::cout << "[plugin-loader] loaded plugin: " << metadata.id << "\n";
            }
        }
        catch (const std::exception& ex)
        {
            errors.push_back("plugin scan failed: " + std::string(ex.what()));
        }
        catch (...)
        {
            errors.push_back("plugin scan failed: unknown error");
        }

        return errors;
    }
}
