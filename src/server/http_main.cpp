#include <arpa/inet.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "fin/api/ScenarioService.hpp"
#include "fin/app/ScenarioSerialization.hpp"

namespace
{
    struct Options
    {
        int port = 8080;
        // /run-file only opens scenarios under this directory.
        std::filesystem::path root = std::filesystem::current_path();
        std::size_t max_body = 1024 * 1024; // 1 MiB
        unsigned max_connections = 32;
    };

    constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
    constexpr std::size_t kMaxDrainBytes = 1024 * 1024;
    constexpr int kDrainTimeoutMs = 200;

    std::string trim(const std::string &s)
    {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return {};
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    // Writes the request body to a temp INI and deletes it when the scope ends.
    class TempConfig
    {
    public:
        explicit TempConfig(const std::string &body)
        {
            static std::atomic<unsigned long long> counter{0};
            const auto ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path() /
                    ("aiquant_http_" + std::to_string(::getpid()) + "_" + std::to_string(ts) + "_" +
                     std::to_string(counter++) + ".ini");
            std::ofstream out(path_);
            out << body;
        }

        ~TempConfig()
        {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }

        TempConfig(const TempConfig &) = delete;
        TempConfig &operator=(const TempConfig &) = delete;

        std::string string() const { return path_.string(); }

    private:
        std::filesystem::path path_;
    };

    struct Request
    {
        std::string method;
        std::string path;
        std::string body;
    };

    enum class ReadStatus
    {
        Ok,
        Malformed,
        BodyTooLarge
    };

    ReadStatus read_request(int client_fd, std::size_t max_body, Request &req)
    {
        std::string data;
        char buffer[4096];
        std::size_t header_end = std::string::npos;

        while (true)
        {
            const auto received = ::recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
                return ReadStatus::Malformed;
            data.append(buffer, static_cast<std::size_t>(received));
            header_end = data.find("\r\n\r\n");
            if (header_end != std::string::npos)
                break;
            if (data.size() > kMaxHeaderBytes)
                return ReadStatus::Malformed;
        }

        std::istringstream header_stream(data.substr(0, header_end + 2));
        std::string request_line;
        if (!std::getline(header_stream, request_line))
            return ReadStatus::Malformed;
        if (!request_line.empty() && request_line.back() == '\r')
            request_line.pop_back();
        std::istringstream request_line_stream(request_line);
        if (!(request_line_stream >> req.method >> req.path))
            return ReadStatus::Malformed;

        std::size_t content_length = 0;
        std::string header;
        while (std::getline(header_stream, header))
        {
            if (!header.empty() && header.back() == '\r')
                header.pop_back();
            const auto colon = header.find(':');
            if (colon == std::string::npos)
                continue;
            const auto name = header.substr(0, colon);
            const auto value = trim(header.substr(colon + 1));
            if (strcasecmp(name.c_str(), "Content-Length") == 0)
            {
                // from_chars instead of stoul: a junk Content-Length must not throw out of here.
                std::size_t parsed = 0;
                const auto *first = value.data();
                const auto *last = first + value.size();
                const auto [ptr, ec] = std::from_chars(first, last, parsed);
                if (ec != std::errc{} || ptr != last)
                    return ReadStatus::Malformed;
                content_length = parsed;
            }
        }

        if (content_length > max_body)
            return ReadStatus::BodyTooLarge;

        req.body = data.substr(header_end + 4);
        while (req.body.size() < content_length)
        {
            const auto received = ::recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
                break;
            req.body.append(buffer, static_cast<std::size_t>(received));
            if (req.body.size() > max_body)
                return ReadStatus::BodyTooLarge;
        }
        if (req.body.size() > content_length)
            req.body.resize(content_length);
        return ReadStatus::Ok;
    }

    void send_response(int client_fd, int status, std::string_view status_text, const std::string &payload)
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
        out << "{\"error\": " << std::quoted(message) << "}\n";
        return out.str();
    }

    // When we answer before reading the whole request (busy, body too large, malformed), the
    // client may still be writing. Closing right away gives it a broken pipe instead of our
    // status code, so half-close and drain briefly first.
    void drain_and_close(int client_fd)
    {
        ::shutdown(client_fd, SHUT_WR);

        timeval timeout{};
        timeout.tv_sec = kDrainTimeoutMs / 1000;
        timeout.tv_usec = (kDrainTimeoutMs % 1000) * 1000;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        char sink[4096];
        std::size_t drained = 0;
        while (drained < kMaxDrainBytes)
        {
            const auto received = ::recv(client_fd, sink, sizeof(sink), 0);
            if (received <= 0)
                break;
            drained += static_cast<std::size_t>(received);
        }
        ::close(client_fd);
    }

    // Owns the accepted socket for the lifetime of one request.
    class SocketGuard
    {
    public:
        explicit SocketGuard(int fd) : fd_(fd) {}

        ~SocketGuard()
        {
            if (drain_)
                drain_and_close(fd_);
            else
                ::close(fd_);
        }

        SocketGuard(const SocketGuard &) = delete;
        SocketGuard &operator=(const SocketGuard &) = delete;

        void drain_before_close() { drain_ = true; }

    private:
        int fd_;
        bool drain_ = false;
    };

    enum class PathStatus
    {
        Ok,
        Empty,
        Outside,
        Missing
    };

    // Resolves the requested scenario against the configured root and refuses anything that
    // escapes it, so the service cannot be used to read arbitrary files off the host.
    PathStatus resolve_scenario_path(const std::filesystem::path &root, const std::string &raw,
                                     std::filesystem::path &resolved)
    {
        const auto requested = trim(raw);
        if (requested.empty())
            return PathStatus::Empty;

        std::error_code ec;
        std::filesystem::path candidate(requested);
        if (candidate.is_relative())
            candidate = root / candidate;

        candidate = std::filesystem::weakly_canonical(candidate, ec);
        if (ec)
            candidate = candidate.lexically_normal();

        const auto relative = candidate.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..")
            return PathStatus::Outside;

        if (!std::filesystem::is_regular_file(candidate, ec))
            return PathStatus::Missing;

        resolved = candidate;
        return PathStatus::Ok;
    }

    void handle_request(const fin::api::ScenarioService &service, const Options &opts, int client)
    {
        SocketGuard guard(client);

        Request req;
        switch (read_request(client, opts.max_body, req))
        {
        case ReadStatus::Malformed:
            send_response(client, 400, "Bad Request", json_error("Malformed request"));
            guard.drain_before_close();
            return;
        case ReadStatus::BodyTooLarge:
            send_response(client, 413, "Content Too Large",
                          json_error("Request body exceeds the " + std::to_string(opts.max_body) + " byte limit"));
            guard.drain_before_close();
            return;
        case ReadStatus::Ok:
            break;
        }

        if (req.method == "GET" && req.path == "/health")
        {
            send_response(client, 200, "OK", "{\"status\": \"ok\"}\n");
            return;
        }

        if (req.method != "POST")
        {
            send_response(client, 405, "Method Not Allowed",
                          json_error("Only POST is supported, apart from GET /health"));
            return;
        }

        try
        {
            if (req.path == "/run-file")
            {
                std::filesystem::path scenario;
                switch (resolve_scenario_path(opts.root, req.body, scenario))
                {
                case PathStatus::Empty:
                    send_response(client, 400, "Bad Request", json_error("Missing scenario path"));
                    return;
                case PathStatus::Outside:
                    send_response(client, 403, "Forbidden",
                                  json_error("Scenario path is outside the configured root directory"));
                    return;
                case PathStatus::Missing:
                    send_response(client, 404, "Not Found", json_error("Scenario file not found"));
                    return;
                case PathStatus::Ok:
                    break;
                }

                fin::app::ScenarioConfig cfg{};
                try
                {
                    cfg = service.load_file(scenario.string());
                }
                catch (const std::exception &ex)
                {
                    send_response(client, 400, "Bad Request", json_error(ex.what()));
                    return;
                }

                const auto result = service.run(cfg);
                send_response(client, 200, "OK", fin::app::scenario_result_to_json(cfg, result));
            }
            else if (req.path == "/run-config")
            {
                if (req.body.empty())
                {
                    send_response(client, 400, "Bad Request", json_error("Empty scenario payload"));
                    return;
                }

                const TempConfig temp_config(req.body);
                fin::app::ScenarioConfig cfg{};
                try
                {
                    cfg = service.load_file(temp_config.string());
                }
                catch (const std::exception &ex)
                {
                    send_response(client, 400, "Bad Request", json_error(ex.what()));
                    return;
                }

                const auto result = service.run(cfg);
                send_response(client, 200, "OK", fin::app::scenario_result_to_json(cfg, result));
            }
            else
            {
                send_response(client, 404, "Not Found", json_error("Unknown endpoint"));
            }
        }
        catch (const std::invalid_argument &ex)
        {
            // Bad configuration values reach us as invalid_argument.
            send_response(client, 400, "Bad Request", json_error(ex.what()));
        }
        catch (const std::runtime_error &ex)
        {
            // Well-formed scenario the engine cannot run, e.g. too few candles for the warmup.
            send_response(client, 422, "Unprocessable Content", json_error(ex.what()));
        }
        catch (const std::exception &ex)
        {
            send_response(client, 500, "Internal Server Error", json_error(ex.what()));
        }
    }

    void print_usage()
    {
        std::cerr << "Usage: aiquant_http [--port N] [--root DIR] [--max-body BYTES] [--max-connections N]\n"
                  << "  --root             directory /run-file may read scenarios from (default: cwd)\n"
                  << "  --max-body         maximum request body in bytes (default: 1048576)\n"
                  << "  --max-connections  requests served concurrently before 503 (default: 32)\n";
    }

    bool parse_args(int argc, char **argv, Options &opts)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg(argv[i]);
            if (arg == "--help" || arg == "-h")
                return false;

            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << arg << "\n";
                return false;
            }
            const std::string value(argv[++i]);

            try
            {
                if (arg == "--port")
                    opts.port = std::stoi(value);
                else if (arg == "--root")
                    opts.root = value;
                else if (arg == "--max-body")
                    opts.max_body = static_cast<std::size_t>(std::stoull(value));
                else if (arg == "--max-connections")
                    opts.max_connections = static_cast<unsigned>(std::stoul(value));
                else
                {
                    std::cerr << "Unknown option " << arg << "\n";
                    return false;
                }
            }
            catch (const std::exception &)
            {
                std::cerr << "Invalid value for " << arg << ": " << value << "\n";
                return false;
            }
        }
        return true;
    }
}

int main(int argc, char **argv)
{
    Options opts;
    if (!parse_args(argc, argv, opts))
    {
        print_usage();
        return 2;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(opts.root, ec))
    {
        std::cerr << "Root directory does not exist: " << opts.root << "\n";
        return 2;
    }
    opts.root = std::filesystem::weakly_canonical(opts.root, ec);
    if (ec)
    {
        std::cerr << "Cannot resolve root directory: " << ec.message() << "\n";
        return 2;
    }

    // A client that disappears mid-response must not take the server down with SIGPIPE.
    std::signal(SIGPIPE, SIG_IGN);

    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
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
    addr.sin_port = htons(static_cast<uint16_t>(opts.port));

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        std::perror("bind");
        ::close(server_fd);
        return 1;
    }

    if (listen(server_fd, 64) < 0)
    {
        std::perror("listen");
        ::close(server_fd);
        return 1;
    }

    std::cout << "AiQuant HTTP service listening on port " << opts.port << "\n"
              << "  scenario root: " << opts.root << "\n"
              << "  max body: " << opts.max_body << " bytes, max concurrent requests: "
              << opts.max_connections << std::endl;

    const fin::api::ScenarioService service;
    std::atomic<unsigned> active{0};

    while (true)
    {
        const int client = ::accept(server_fd, nullptr, nullptr);
        if (client < 0)
        {
            if (errno == EINTR)
                continue;
            std::perror("accept");
            break;
        }

        if (active.load() >= opts.max_connections)
        {
            send_response(client, 503, "Service Unavailable", json_error("Server busy, retry later"));
            drain_and_close(client);
            continue;
        }

        ++active;
        // ScenarioService is stateless and run_scenario only touches its arguments, so requests
        // can be served concurrently. The thread owns the socket and closes it.
        std::thread([client, &service, &opts, &active] {
            handle_request(service, opts, client); // owns and closes the socket
            --active;
        }).detach();
    }

    ::close(server_fd);
    return 0;
}
