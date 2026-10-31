#ifndef SERVER_H
#define SERVER_H
#include <string>
#include <functional>

struct Request  { std::string method, path, body; };
struct Response { int status; std::string type; std::string body; };

// Minimal multi-threaded HTTP/1.1 server (Windows + Linux/macOS).
class Server {
public:
    using Handler = std::function<Response(const Request&)>;
    Server(int port, Handler h) : port(port), handler(std::move(h)) {}
    int run();   // blocks; returns non-zero on startup failure
private:
    int port;
    Handler handler;
};
#endif
