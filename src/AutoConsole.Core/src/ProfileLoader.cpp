#include "AutoConsole/Core/ProfileLoader.h"

#include <fstream>
#include <regex>
#include <sstream>

namespace
{
    std::string trim_copy(const std::string& value)
    {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
        {
            return "";
        }

        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    bool has_key(const std::string& jsonText, const std::string& key)
    {
        const std::regex keyPattern("\\\"" + key + "\\\"\\s*:");
        return std::regex_search(jsonText, keyPattern);
    }

    bool looks_like_json_object(const std::string& jsonText)
    {
        const std::string trimmed = trim_copy(jsonText);
        if (trimmed.empty())
        {
            return false;
        }

        return trimmed.front() == '{' && trimmed.back() == '}';
    }

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
            errorMessage = "file not found: " + filePath;
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string jsonText = buffer.str();

        if (!looks_like_json_object(jsonText))
        {
            errorMessage = "invalid JSON: expected a JSON object";
            return std::nullopt;
        }

        std::vector<std::string> missingFields;
        if (!has_key(jsonText, "id"))
        {
            missingFields.push_back("id");
        }
        if (!has_key(jsonText, "name"))
        {
            missingFields.push_back("name");
        }
        if (!has_key(jsonText, "command"))
        {
            missingFields.push_back("command");
        }

        if (!missingFields.empty())
        {
            errorMessage = "missing required fields: ";
            for (std::size_t i = 0; i < missingFields.size(); ++i)
            {
                if (i > 0)
                {
                    errorMessage += ", ";
                }
                errorMessage += missingFields[i];
            }
            return std::nullopt;
        }

        auto id = extract_string_value(jsonText, "id");
        auto name = extract_string_value(jsonText, "name");
        auto command = extract_string_value(jsonText, "command");

        if (!id.has_value() || !name.has_value() || !command.has_value())
        {
            errorMessage = "invalid JSON: id, name, and command must be string values";
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
