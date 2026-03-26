#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "ApiServer.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "AutoConsole/Abstractions/SessionState.h"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    struct HttpRequest
    {
        std::string method;
        std::string path;
        std::string query;
        std::unordered_map<std::string, std::string> headers;
        std::string body;
    };

    std::string json_escape(const std::string& value)
    {
        std::string result;
        result.reserve(value.size() + 8);
        for (const char ch : value)
        {
            switch (ch)
            {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(ch); break;
            }
        }
        return result;
    }

    std::string make_success(const std::string& message, const std::string& dataJson)
    {
        std::ostringstream oss;
        oss << "{\"success\":true,\"message\":\"" << json_escape(message) << "\",\"data\":" << dataJson << "}";
        return oss.str();
    }

    std::string make_failure(const std::string& message)
    {
        std::ostringstream oss;
        oss << "{\"success\":false,\"message\":\"" << json_escape(message) << "\"}";
        return oss.str();
    }

    std::unordered_map<std::string, std::string> parse_query(const std::string& query)
    {
        std::unordered_map<std::string, std::string> result;
        std::istringstream iss(query);
        std::string token;
        while (std::getline(iss, token, '&'))
        {
            const auto pos = token.find('=');
            if (pos == std::string::npos)
            {
                continue;
            }
            result[token.substr(0, pos)] = token.substr(pos + 1);
        }
        return result;
    }

    std::optional<std::string> extract_json_string(const std::string& jsonText, const std::string& key)
    {
        const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
        std::smatch match;
        if (!std::regex_search(jsonText, match, pattern) || match.size() < 2)
        {
            return std::nullopt;
        }
        return match[1].str();
    }

    std::unordered_map<std::string, std::string> extract_json_object(const std::string& jsonText, const std::string& key)
    {
        std::unordered_map<std::string, std::string> result;
        const std::regex objPattern("\\\"" + key + "\\\"\\s*:\\s*\\{([\\s\\S]*?)\\}");
        std::smatch objMatch;
        if (!std::regex_search(jsonText, objMatch, objPattern) || objMatch.size() < 2)
        {
            return result;
        }

        const std::string content = objMatch[1].str();
        const std::regex itemPattern("\\\"([^\\\"]+)\\\"\\s*:\\s*(\\\"([^\\\"]*)\\\"|[-]?[0-9]+|true|false|null)");
        auto begin = std::sregex_iterator(content.begin(), content.end(), itemPattern);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it)
        {
            const std::string keyValue = (*it)[1].str();
            std::string value = (*it)[2].str();
            if (!value.empty() && value.front() == '"' && value.back() == '"')
            {
                value = (*it)[3].str();
            }
            result[keyValue] = value;
        }

        return result;
    }

    bool read_http_request(SOCKET client, HttpRequest& request)
    {
        std::string data;
        char buffer[4096];
        while (data.find("\r\n\r\n") == std::string::npos)
        {
            const int received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
            if (received <= 0)
            {
                return false;
            }
            data.append(buffer, buffer + received);
            if (data.size() > 1024 * 1024)
            {
                return false;
            }
        }

        const auto headerEnd = data.find("\r\n\r\n");
        const std::string headerText = data.substr(0, headerEnd);
        request.body = data.substr(headerEnd + 4);

        std::istringstream headerStream(headerText);
        std::string requestLine;
        if (!std::getline(headerStream, requestLine))
        {
            return false;
        }

        if (!requestLine.empty() && requestLine.back() == '\r')
        {
            requestLine.pop_back();
        }

        std::istringstream requestLineStream(requestLine);
        std::string target;
        requestLineStream >> request.method >> target;
        if (request.method.empty() || target.empty())
        {
            return false;
        }

        const auto queryPos = target.find('?');
        if (queryPos != std::string::npos)
        {
            request.path = target.substr(0, queryPos);
            request.query = target.substr(queryPos + 1);
        }
        else
        {
            request.path = target;
        }

        std::string headerLine;
        while (std::getline(headerStream, headerLine))
        {
            if (!headerLine.empty() && headerLine.back() == '\r')
            {
                headerLine.pop_back();
            }
            if (headerLine.empty())
            {
                continue;
            }
            const auto colonPos = headerLine.find(':');
            if (colonPos == std::string::npos)
            {
                continue;
            }
            std::string key = headerLine.substr(0, colonPos);
            std::string value = headerLine.substr(colonPos + 1);
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            request.headers[key] = value;
        }

        const auto it = request.headers.find("content-length");
        if (it != request.headers.end())
        {
            size_t contentLength = 0;
            try
            {
                contentLength = static_cast<size_t>(std::stoi(it->second));
            }
            catch (...)
            {
                return false;
            }
            while (request.body.size() < contentLength)
            {
                const int received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
                if (received <= 0)
                {
                    return false;
                }
                request.body.append(buffer, buffer + received);
            }
            if (request.body.size() > contentLength)
            {
                request.body.resize(contentLength);
            }
        }

        return true;
    }

    bool send_http_response(SOCKET client, int statusCode, const std::string& body)
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << statusCode << " ";
        switch (statusCode)
        {
        case 200: oss << "OK"; break;
        case 400: oss << "Bad Request"; break;
        case 404: oss << "Not Found"; break;
        default: oss << "Internal Server Error"; break;
        }
        oss << "\r\n";
        oss << "Content-Type: application/json\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Connection: close\r\n";
        oss << "\r\n";
        oss << body;

        const std::string response = oss.str();
        size_t sent = 0;
        while (sent < response.size())
        {
            const int chunk = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
            if (chunk <= 0)
            {
                return false;
            }
            sent += static_cast<size_t>(chunk);
        }
        return true;
    }

    std::string session_state_to_string(AutoConsole::Abstractions::SessionState state)
    {
        switch (state)
        {
        case AutoConsole::Abstractions::SessionState::Created:
            return "Created";
        case AutoConsole::Abstractions::SessionState::Starting:
            return "Starting";
        case AutoConsole::Abstractions::SessionState::Running:
            return "Running";
        case AutoConsole::Abstractions::SessionState::Stopped:
            return "Stopped";
        case AutoConsole::Abstractions::SessionState::Failed:
            return "Failed";
        default:
            return "Unknown";
        }
    }
}

namespace AutoConsole::Cli
{
    ApiServer::ApiServer(
        AutoConsole::Core::CoreRuntime& runtime,
        ProfileResolver profileResolver,
        ApiLogBuffer* logBuffer)
        : runtime_(runtime),
          profileResolver_(std::move(profileResolver)),
          logBuffer_(logBuffer)
    {
    }

    ApiServer::~ApiServer()
    {
        stop();
    }

    bool ApiServer::start(int port, std::string& errorMessage)
    {
        if (running_)
        {
            return true;
        }

        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            errorMessage = "WSAStartup failed";
            return false;
        }

        SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET)
        {
            WSACleanup();
            errorMessage = "failed to create socket";
            return false;
        }

        sockaddr_in service{};
        service.sin_family = AF_INET;
        service.sin_port = htons(static_cast<u_short>(port));
        inet_pton(AF_INET, "127.0.0.1", &service.sin_addr);

        const int yes = 1;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

        if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR)
        {
            closesocket(listenSocket);
            WSACleanup();
            errorMessage = "failed to bind port";
            return false;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
        {
            closesocket(listenSocket);
            WSACleanup();
            errorMessage = "failed to listen";
            return false;
        }

        listenSocket_ = reinterpret_cast<void*>(listenSocket);
        running_ = true;
        std::cout << "[api] starting server on 127.0.0.1:" << port << "\n";
        serverThread_ = std::thread([this]()
        {
            run_loop();
        });

        return true;
    }

    void ApiServer::stop()
    {
        if (!running_)
        {
            return;
        }

        std::cout << "[api] stopping server\n";
        running_ = false;
        if (listenSocket_ != nullptr)
        {
            const SOCKET socketHandle = reinterpret_cast<SOCKET>(listenSocket_);
            shutdown(socketHandle, SD_BOTH);
            closesocket(socketHandle);
            listenSocket_ = nullptr;
        }

        if (serverThread_.joinable())
        {
            serverThread_.join();
        }

        WSACleanup();
        std::cout << "[api] server stopped\n";
    }

    void ApiServer::run_loop()
    {
        const SOCKET listenSocket = reinterpret_cast<SOCKET>(listenSocket_);
        while (running_)
        {
            sockaddr_in clientAddr{};
            int clientSize = sizeof(clientAddr);
            SOCKET client = accept(listenSocket, reinterpret_cast<SOCKADDR*>(&clientAddr), &clientSize);
            if (client == INVALID_SOCKET)
            {
                continue;
            }

            HttpRequest request;
            bool ok = read_http_request(client, request);
            if (!ok)
            {
                send_http_response(client, 400, make_failure("invalid request"));
                closesocket(client);
                continue;
            }

            int statusCode = 200;
            std::string responseBody;

            if (request.method == "GET" && request.path == "/sessions")
            {
                const auto sessions = runtime_.sessions();
                std::ostringstream data;
                data << "[";
                bool first = true;
                for (const auto& session : sessions)
                {
                    if (!first)
                    {
                        data << ",";
                    }
                    first = false;
                    data << "{";
                    data << "\"sessionId\":\"" << json_escape(session.id) << "\",";
                    data << "\"profileId\":\"" << json_escape(session.profileId) << "\",";
                    data << "\"profileName\":\"" << json_escape(session.profileName) << "\",";
                    data << "\"state\":\"" << json_escape(session_state_to_string(session.state)) << "\"";
                    if (session.hasExitCode)
                    {
                        data << ",\"exitCode\":" << session.exitCode;
                    }
                    data << "}";
                }
                data << "]";
                responseBody = make_success("ok", data.str());
            }
            else if (request.method == "POST" && request.path == "/start")
            {
                const auto profileId = extract_json_string(request.body, "profile");
                if (!profileId.has_value())
                {
                    statusCode = 400;
                    responseBody = make_failure("missing profile");
                }
                else
                {
                    std::string loadError;
                    const auto profile = profileResolver_(*profileId, loadError);
                    if (!profile.has_value())
                    {
                        statusCode = 404;
                        responseBody = make_failure(loadError.empty() ? "profile not found" : loadError);
                    }
                    else
                    {
                        const auto startResult = runtime_.start_session(*profile);
                        if (!startResult.started)
                        {
                            statusCode = 500;
                            responseBody = make_failure(startResult.errorMessage.empty() ? "failed to start" : startResult.errorMessage);
                        }
                        else
                        {
                            std::ostringstream data;
                            data << "{";
                            data << "\"sessionId\":\"" << json_escape(startResult.session.id) << "\"";
                            data << "}";
                            responseBody = make_success("ok", data.str());
                        }
                    }
                }
            }
            else if (request.method == "POST" && request.path == "/send")
            {
                const auto sessionId = extract_json_string(request.body, "sessionId");
                const auto text = extract_json_string(request.body, "text");
                if (!sessionId.has_value() || !text.has_value())
                {
                    statusCode = 400;
                    responseBody = make_failure("missing sessionId or text");
                }
                else
                {
                    std::string errorMessage;
                    if (!runtime_.send_input(*sessionId, *text, errorMessage))
                    {
                        statusCode = (errorMessage == "session not found") ? 404 : 500;
                        responseBody = make_failure(errorMessage);
                    }
                    else
                    {
                        responseBody = make_success("sent", "{}");
                    }
                }
            }
            else if (request.method == "POST" && request.path == "/stop")
            {
                const auto sessionId = extract_json_string(request.body, "sessionId");
                if (!sessionId.has_value())
                {
                    statusCode = 400;
                    responseBody = make_failure("missing sessionId");
                }
                else
                {
                    std::string errorMessage;
                    if (!runtime_.stop_session(*sessionId, errorMessage))
                    {
                        if (errorMessage.rfind("invalid sessionId:", 0) == 0)
                        {
                            statusCode = 404;
                            responseBody = make_failure("session not found");
                        }
                        else
                        {
                            statusCode = 400;
                            responseBody = make_failure(errorMessage.empty() ? "failed to stop session" : errorMessage);
                        }
                    }
                    else
                    {
                        responseBody = make_success("stopped", "{}");
                    }
                }
            }
            else if (request.method == "POST" && request.path == "/plugin/execute")
            {
                const auto pluginId = extract_json_string(request.body, "pluginId");
                const auto action = extract_json_string(request.body, "action");
                if (!pluginId.has_value() || !action.has_value())
                {
                    statusCode = 400;
                    responseBody = make_failure("missing pluginId or action");
                }
                else
                {
                    const auto args = extract_json_object(request.body, "args");
                    std::string actionMessage;
                    if (!runtime_.call_plugin_action(*pluginId, *action, args, actionMessage))
                    {
                        if (actionMessage == "plugin not found")
                        {
                            statusCode = 404;
                        }
                        else
                        {
                            statusCode = 400;
                        }
                        responseBody = make_failure(actionMessage.empty() ? "plugin action failed" : actionMessage);
                    }
                    else
                    {
                        std::ostringstream data;
                        data << "{\"message\":\"" << json_escape(actionMessage) << "\"}";
                        responseBody = make_success("ok", data.str());
                    }
                }
            }
            else if (request.method == "GET" && request.path == "/logs")
            {
                const auto params = parse_query(request.query);
                const auto it = params.find("sessionId");
                if (it == params.end() || it->second.empty())
                {
                    statusCode = 400;
                    responseBody = make_failure("missing sessionId");
                }
                else if (!logBuffer_)
                {
                    statusCode = 500;
                    responseBody = make_failure("log buffer unavailable");
                }
                else
                {
                    size_t limit = 100;
                    const auto limitIt = params.find("limit");
                    if (limitIt != params.end())
                    {
                        try
                        {
                            limit = static_cast<size_t>(std::max(1, std::stoi(limitIt->second)));
                        }
                        catch (...)
                        {
                            limit = 100;
                        }
                    }
                    limit = std::min<size_t>(limit, 1000);

                    const auto logs = logBuffer_->get(it->second, limit);
                    std::ostringstream data;
                    data << "[";
                    bool first = true;
                    for (const auto& log : logs)
                    {
                        if (!first)
                        {
                            data << ",";
                        }
                        first = false;
                        data << "{";
                        data << "\"timestamp\":\"" << json_escape(log.timestamp) << "\",";
                        data << "\"type\":\"" << json_escape(log.type) << "\",";
                        data << "\"text\":\"" << json_escape(log.text) << "\"";
                        data << "}";
                    }
                    data << "]";
                    responseBody = make_success("ok", data.str());
                }
            }
            else
            {
                statusCode = 404;
                responseBody = make_failure("not found");
            }

            send_http_response(client, statusCode, responseBody);
            closesocket(client);
        }
    }
}
