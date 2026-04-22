#include "HttpServer.h"
#include "../Analyzer/Analyzer.h"
#include "../DiskManagement/DiskManagement.h"
#include "../FileOperations/FileOperations.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace HttpServer
{
    struct HttpRequest
    {
        std::string method;
        std::string target;
        std::string path;
        std::string body;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> query;
    };

    struct HttpResponse
    {
        int status = 200;
        std::string statusText = "OK";
        std::string contentType = "application/json";
        std::string body;
        bool cors = true;
    };

    // Escapa caracteres especiales de JSON en una cadena
    static std::string JsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '"')
                out += "\\\"";
            else if (c == '\\')
                out += "\\\\";
            else if (c == '\n')
                out += "\\n";
            else if (c == '\r')
                out += "\\r";
            else if (c == '\t')
                out += "\\t";
            else
                out += c;
        }
        return out;
    }

    // Extrae el valor del campo "commands" de un cuerpo JSON simple
    // Soporta: {"commands":"..."} o {"commands": "..."}
    static std::string ExtractCommands(const std::string &body)
    {
        const std::string key = "\"commands\"";
        auto pos = body.find(key);
        if (pos == std::string::npos)
            return "";

        pos = body.find('"', pos + key.size());
        if (pos == std::string::npos)
            return "";
        pos++;

        std::string result;
        bool escape = false;
        for (size_t i = pos; i < body.size(); i++)
        {
            char c = body[i];
            if (escape)
            {
                if (c == 'n')
                    result += '\n';
                else if (c == 'r')
                    result += '\r';
                else if (c == 't')
                    result += '\t';
                else if (c == '\\')
                    result += '\\';
                else if (c == '"')
                    result += '"';
                else
                    result += c;
                escape = false;
            }
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                break;
            else
                result += c;
        }
        return result;
    }

    static std::string UrlDecode(const std::string &value)
    {
        std::string out;
        out.reserve(value.size());
        for (size_t i = 0; i < value.size(); i++)
        {
            if (value[i] == '%' && i + 2 < value.size())
            {
                std::string hex = value.substr(i + 1, 2);
                char *end = nullptr;
                long decoded = std::strtol(hex.c_str(), &end, 16);
                if (end && *end == '\0')
                {
                    out += static_cast<char>(decoded);
                    i += 2;
                }
                else
                {
                    out += value[i];
                }
            }
            else if (value[i] == '+')
                out += ' ';
            else
                out += value[i];
        }
        return out;
    }

    static std::map<std::string, std::string> ParseQuery(const std::string &target, std::string &path)
    {
        std::map<std::string, std::string> query;
        size_t qPos = target.find('?');
        path = qPos == std::string::npos ? target : target.substr(0, qPos);
        if (qPos == std::string::npos)
            return query;

        std::string raw = target.substr(qPos + 1);
        std::stringstream ss(raw);
        std::string part;
        while (std::getline(ss, part, '&'))
        {
            size_t eq = part.find('=');
            std::string key = UrlDecode(eq == std::string::npos ? part : part.substr(0, eq));
            std::string value = eq == std::string::npos ? "" : UrlDecode(part.substr(eq + 1));
            query[key] = value;
        }
        return query;
    }

    static std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return std::tolower(c); });
        return s;
    }

    static bool ParseRequest(const std::string &raw, HttpRequest &req)
    {
        size_t headerEnd = raw.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
            return false;

        std::stringstream headers(raw.substr(0, headerEnd));
        std::string requestLine;
        if (!std::getline(headers, requestLine))
            return false;
        if (!requestLine.empty() && requestLine.back() == '\r')
            requestLine.pop_back();

        std::stringstream first(requestLine);
        first >> req.method >> req.target;
        if (req.method.empty() || req.target.empty())
            return false;

        req.query = ParseQuery(req.target, req.path);

        std::string line;
        while (std::getline(headers, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            std::string key = Lower(line.substr(0, colon));
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ')
                value.erase(value.begin());
            req.headers[key] = value;
        }

        req.body = raw.substr(headerEnd + 4);
        return true;
    }

    static std::string StatusText(int status)
    {
        if (status == 204) return "No Content";
        if (status == 400) return "Bad Request";
        if (status == 404) return "Not Found";
        if (status == 405) return "Method Not Allowed";
        if (status == 500) return "Internal Server Error";
        return "OK";
    }

    static std::string BuildResponse(const HttpResponse &response)
    {
        std::ostringstream out;
        out << "HTTP/1.1 " << response.status << " " << response.statusText << "\r\n";
        out << "Content-Type: " << response.contentType << "\r\n";
        out << "Content-Length: " << response.body.size() << "\r\n";
        out << "Connection: close\r\n";
        if (response.cors)
        {
            out << "Access-Control-Allow-Origin: *\r\n";
            out << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
            out << "Access-Control-Allow-Headers: Content-Type\r\n";
        }
        out << "\r\n";
        out << response.body;
        return out.str();
    }

    static HttpResponse JsonResponse(int status, const std::string &body)
    {
        HttpResponse res;
        res.status = status;
        res.statusText = StatusText(status);
        res.body = body;
        return res;
    }

    static HttpResponse HandleRequest(const HttpRequest &req)
    {
        if (req.method == "OPTIONS")
        {
            HttpResponse res;
            res.status = 204;
            res.statusText = StatusText(204);
            return res;
        }

        if (req.path == "/health" && req.method == "GET")
            return JsonResponse(200, "{\"ok\":true,\"service\":\"MIA_P2\"}");

        if (req.path == "/execute" && req.method == "POST")
        {
            std::string commands = ExtractCommands(req.body);
            std::string output;
            if (commands.empty())
                output = "Error: campo 'commands' no encontrado en el cuerpo JSON";
            else
                output = Analyzer::AnalyzeScript(commands);
            return JsonResponse(200, "{\"output\":\"" + JsonEscape(output) + "\"}");
        }

        if (req.path == "/fs/mounted" && req.method == "GET")
        {
            std::string json = "{\"ok\":true,\"items\":[";
            bool first = true;
            for (const auto &entry : DiskManagement::MountMap)
            {
                const auto &id = entry.first;
                const auto &mp = entry.second;
                if (!first) json += ",";
                first = false;
                json += "{\"id\":\"" + JsonEscape(id) + "\",\"name\":\"" + JsonEscape(mp.name) + "\",\"diskPath\":\"" + JsonEscape(mp.diskPath) + "\"}";
            }
            json += "]}";
            return JsonResponse(200, json);
        }

        if (req.path == "/fs/browse" && req.method == "GET")
        {
            auto id = req.query.find("id");
            auto path = req.query.find("path");
            if (id == req.query.end() || path == req.query.end())
                return JsonResponse(400, "{\"ok\":false,\"error\":\"falta id o path\"}");
            return JsonResponse(200, FileOperations::BrowseJson(id->second, path->second));
        }

        if (req.path == "/fs/file" && req.method == "GET")
        {
            auto id = req.query.find("id");
            auto path = req.query.find("path");
            if (id == req.query.end() || path == req.query.end())
                return JsonResponse(400, "{\"ok\":false,\"error\":\"falta id o path\"}");
            return JsonResponse(200, FileOperations::ReadFileJson(id->second, path->second));
        }

        if (req.path == "/report" && req.method == "GET")
        {
            auto filePath = req.query.find("path");
            if (filePath == req.query.end())
                return JsonResponse(400, "{\"error\":\"falta parametro path\"}");

            const std::string &path = filePath->second;
            if (!std::filesystem::exists(path))
                return JsonResponse(404, "{\"error\":\"archivo no encontrado\"}");

            std::string ext;
            auto dotPos = path.rfind('.');
            if (dotPos != std::string::npos)
                ext = path.substr(dotPos + 1);

            HttpResponse res;
            res.contentType = "application/octet-stream";
            if (ext == "jpg" || ext == "jpeg") res.contentType = "image/jpeg";
            else if (ext == "png") res.contentType = "image/png";
            else if (ext == "svg") res.contentType = "image/svg+xml";
            else if (ext == "pdf") res.contentType = "application/pdf";
            else if (ext == "txt") res.contentType = "text/plain; charset=utf-8";

            std::ifstream f(path, std::ios::binary);
            res.body.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return res;
        }

        return JsonResponse(404, "{\"error\":\"endpoint no encontrado\"}");
    }

    static void HandleClient(int clientFd)
    {
        std::string raw;
        char buffer[4096];
        size_t expectedLength = 0;
        bool haveHeaders = false;

        while (true)
        {
            ssize_t readBytes = recv(clientFd, buffer, sizeof(buffer), 0);
            if (readBytes <= 0)
                break;
            raw.append(buffer, readBytes);

            size_t headerEnd = raw.find("\r\n\r\n");
            if (!haveHeaders && headerEnd != std::string::npos)
            {
                haveHeaders = true;
                HttpRequest partial;
                if (ParseRequest(raw, partial))
                {
                    size_t contentLength = 0;
                    auto it = partial.headers.find("content-length");
                    if (it != partial.headers.end())
                        contentLength = static_cast<size_t>(std::stoul(it->second));
                    expectedLength = headerEnd + 4 + contentLength;
                }
            }

            if (haveHeaders && raw.size() >= expectedLength)
                break;
        }

        HttpResponse response;
        HttpRequest request;
        try
        {
            if (!ParseRequest(raw, request))
                response = JsonResponse(400, "{\"error\":\"solicitud HTTP invalida\"}");
            else
                response = HandleRequest(request);
        }
        catch (const std::exception &e)
        {
            response = JsonResponse(500, "{\"error\":\"" + JsonEscape(e.what()) + "\"}");
        }

        std::string payload = BuildResponse(response);
        send(clientFd, payload.data(), payload.size(), 0);
        close(clientFd);
    }

    void Start(int port)
    {
        int serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0)
            throw std::runtime_error("No se pudo crear socket HTTP");

        int opt = 1;
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(static_cast<uint16_t>(port));

        if (bind(serverFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
        {
            std::string error = std::strerror(errno);
            close(serverFd);
            throw std::runtime_error("No se pudo enlazar puerto " + std::to_string(port) + ": " + error);
        }

        if (listen(serverFd, 32) < 0)
        {
            std::string error = std::strerror(errno);
            close(serverFd);
            throw std::runtime_error("No se pudo iniciar escucha HTTP: " + error);
        }

        std::cout << "Servidor iniciado en http://0.0.0.0:" << port << "\n";
        std::cout << "Endpoint: POST /execute  body: {\"commands\":\"...\"}\n";

        while (true)
        {
            int clientFd = accept(serverFd, nullptr, nullptr);
            if (clientFd < 0)
                continue;
            std::thread(HandleClient, clientFd).detach();
        }
    }

} // namespace HttpServer
