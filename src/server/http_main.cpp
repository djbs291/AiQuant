#include <arpa/inet.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "fin/api/ScenarioService.hpp"
#include "fin/app/ScenarioSerialization.hpp"

namespace
{
    std::string trim(const std::string &s)
    {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return {};
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    std::filesystem::path write_temp_config(const std::string &body)
    {
        const auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        auto path = std::filesystem::temp_directory_path() / ("aiquant_http_" + std::to_string(ts) + ".ini");
        std::ofstream out(path);
        out << body;
        return path;
    }

    bool read_request(int client_fd, std::string &method, std::string &path, std::string &body)
    {
        std::string header_block;
        std::string buffer;
        buffer.resize(4096);
        ssize_t received = 0;
        bool header_done = false;
        std::size_t content_length = 0;

        while (true)
        {
            received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
            if (received <= 0)
                return false;
            header_block.append(buffer.data(), received);
            auto header_end = header_block.find("\r\n\r\n");
            if (header_end != std::string::npos)
            {
                header_done = true;
                std::string headers = header_block.substr(0, header_end + 4);
                std::istringstream header_stream(headers);
                std::string request_line;
                if (!std::getline(header_stream, request_line))
                    return false;
                if (!request_line.empty() && request_line.back() == '\r')
                    request_line.pop_back();
                std::istringstream request_line_stream(request_line);
                request_line_stream >> method >> path;
                std::string header_name;
                while (std::getline(header_stream, header_name))
                {
                    if (!header_name.empty() && header_name.back() == '\r')
                        header_name.pop_back();
                    auto colon = header_name.find(':');
                    if (colon == std::string::npos)
                        continue;
                    auto name = header_name.substr(0, colon);
                    auto value = trim(header_name.substr(colon + 1));
                    if (strcasecmp(name.c_str(), "Content-Length") == 0)
                        content_length = static_cast<std::size_t>(std::stoul(value));
                }

                body = header_block.substr(header_end + 4);
                while (body.size() < content_length)
                {
                    received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
                    if (received <= 0)
                        break;
                    body.append(buffer.data(), received);
                }
                if (body.size() > content_length)
                    body.resize(content_length);
                break;
            }
        }
        return header_done;
    }

    void send_response(int client_fd, int status, const std::string &status_text, const std::string &payload)
    {
        std::ostringstream out;
        out << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << payload.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << payload;
        const auto response = out.str();
        ::send(client_fd, response.data(), response.size(), 0);
    }

    std::string json_error(const std::string &message)
    {
        std::ostringstream out;
        out << "{\"error\": " << std::quoted(message) << "}";
        return out.str();
    }
}

int main(int argc, char **argv)
{
    int port = 8080;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--port")
            port = std::stoi(argv[i + 1]);
    }

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::perror("bind");
        ::close(server_fd);
        return 1;
    }

    if (listen(server_fd, 8) < 0)
    {
        std::perror("listen");
        ::close(server_fd);
        return 1;
    }

    std::cout << "AiQuant HTTP service listening on port " << port << "\n";
    fin::api::ScenarioService service;

    while (true)
    {
        int client = ::accept(server_fd, nullptr, nullptr);
        if (client < 0)
        {
            if (errno == EINTR)
                continue;
            std::perror("accept");
            break;
        }

        std::string method, path, body;
        if (!read_request(client, method, path, body))
        {
            send_response(client, 400, "Bad Request", json_error("Malformed request"));
            ::close(client);
            continue;
        }

        if (method != "POST")
        {
            send_response(client, 405, "Method Not Allowed", json_error("Only POST supported"));
            ::close(client);
            continue;
        }

        try
        {
            if (path == "/run-file")
            {
                auto scenario_path = trim(body);
                if (scenario_path.empty())
                {
                    send_response(client, 400, "Bad Request", json_error("Missing scenario path"));
                }
                else
                {
                    auto result = service.run_file(scenario_path);
                    auto cfg = service.load_file(scenario_path);
                    auto payload = fin::app::scenario_result_to_json(cfg, result);
                    send_response(client, 200, "OK", payload);
                }
            }
            else if (path == "/run-config")
            {
                if (body.empty())
                {
                    send_response(client, 400, "Bad Request", json_error("Empty scenario payload"));
                }
                else
                {
                    auto temp_config = write_temp_config(body);
                    try
                    {
                        auto cfg = service.load_file(temp_config.string());
                        auto result = service.run(cfg);
                        auto payload = fin::app::scenario_result_to_json(cfg, result);
                        send_response(client, 200, "OK", payload);
                    }
                    catch (...)
                    {
                        std::filesystem::remove(temp_config);
                        throw;
                    }
                    std::filesystem::remove(temp_config);
                }
            }
            else
            {
                send_response(client, 404, "Not Found", json_error("Unknown endpoint"));
            }
        }
        catch (const std::exception &ex)
        {
            send_response(client, 500, "Internal Server Error", json_error(ex.what()));
        }

        ::close(client);
    }

    ::close(server_fd);
    return 0;
}
