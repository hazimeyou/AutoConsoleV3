#include "AutoConsole/Core/ProfileLoader.h"

#include <fstream>
#include <regex>
#include <sstream>

namespace
{
    std::optional<std::string> extract_string_value(const std::string& jsonText, const std::string& key)
    {
        const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
        std::smatch match;
        if (!std::regex_search(jsonText, match, pattern) || match.size() < 2)
        {
            return std::nullopt;
        }

        return match[1].str();
    }

    std::vector<std::string> extract_args(const std::string& jsonText)
    {
        std::vector<std::string> args;
        const std::regex arrayPattern("\\\"args\\\"\\s*:\\s*\\[([\\s\\S]*?)\\]", std::regex::icase);
        std::smatch arrayMatch;
        if (!std::regex_search(jsonText, arrayMatch, arrayPattern) || arrayMatch.size() < 2)
        {
            return args;
        }

        const std::string content = arrayMatch[1].str();
        const std::regex itemPattern("\\\"([^\\\"]*)\\\"");
        auto begin = std::sregex_iterator(content.begin(), content.end(), itemPattern);
        const auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it)
        {
            args.push_back((*it)[1].str());
        }

        return args;
    }
}

namespace AutoConsole::Core
{
    std::optional<AutoConsole::Abstractions::Profile> ProfileLoader::load_from_file(
        const std::string& filePath,
        std::string& errorMessage)
    {
        std::ifstream file(filePath);
        if (!file)
        {
            errorMessage = "profile file not found: " + filePath;
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string jsonText = buffer.str();

        auto id = extract_string_value(jsonText, "id");
        auto name = extract_string_value(jsonText, "name");
        auto command = extract_string_value(jsonText, "command");

        if (!id.has_value() || !name.has_value() || !command.has_value())
        {
            errorMessage = "profile JSON must include id, name, and command";
            return std::nullopt;
        }

        AutoConsole::Abstractions::Profile profile{};
        profile.id = *id;
        profile.name = *name;
        profile.command = *command;
        profile.args = extract_args(jsonText);

        auto workingDir = extract_string_value(jsonText, "workingDir");
        if (!workingDir.has_value())
        {
            workingDir = extract_string_value(jsonText, "workingDirectory");
        }
        if (workingDir.has_value())
        {
            profile.workingDir = *workingDir;
        }

        return profile;
    }
}
