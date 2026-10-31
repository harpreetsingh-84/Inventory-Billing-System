#include "Server.h"
#include <iostream>
#include <thread>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
  #define BAD_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int sock_t;
  #define CLOSESOCK close
  #define BAD_SOCK (-1)
#endif

static const size_t MAX_REQUEST = 1024 * 1024;

static const char* reason(int c) {
    switch (c) {
        case 200: return "OK"; case 201: return "Created"; case 400: return "Bad Request";
        case 404: return "Not Found"; case 405: return "Method Not Allowed";
        default: return "Internal Server Error";
    }
}

static void sendAll(sock_t s, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        int n = (int)send(s, d.data() + off, (int)(d.size() - off), 0);
        if (n <= 0) return;
        off += n;
    }
}

static void handleClient(sock_t c, Server::Handler& handler) {
    std::string data;
    char buf[4096];
    size_t hdrEnd;
    while ((hdrEnd = data.find("\r\n\r\n")) == std::string::npos) {
        int n = (int)recv(c, buf, sizeof buf, 0);
        if (n <= 0 || data.size() > MAX_REQUEST) { CLOSESOCK(c); return; }
        data.append(buf, n);
    }
    std::string head = data.substr(0, hdrEnd);
    std::string body = data.substr(hdrEnd + 4);

    std::istringstream line(head.substr(0, head.find("\r\n")));
    Request rq; std::string target, ver;
    line >> rq.method >> target >> ver;
    rq.path = target.substr(0, target.find('?'));

    std::string lower = head;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return std::tolower(ch); });
    size_t len = 0, p = lower.find("content-length:");
    if (p != std::string::npos) { try { len = std::stoul(lower.substr(p + 15)); } catch (...) {} }
    if (len > MAX_REQUEST) { CLOSESOCK(c); return; }
    while (body.size() < len) {
        int n = (int)recv(c, buf, sizeof buf, 0);
        if (n <= 0) { CLOSESOCK(c); return; }
        body.append(buf, n);
    }
    rq.body = body.substr(0, len);

    Response r = handler(rq);
    std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n" +
        "Content-Type: " + r.type + "\r\nContent-Length: " + std::to_string(r.body.size()) +
        "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (rq.method != "HEAD") out += r.body;
    sendAll(c, out);
    CLOSESOCK(c);
}

int Server::run() {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { std::cerr << "WSAStartup failed\n"; return 1; }
#endif
    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == BAD_SOCK) { std::cerr << "Cannot create socket\n"; return 1; }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");   // local machine only
    if (bind(srv, (sockaddr*)&addr, sizeof addr) < 0 || listen(srv, 32) < 0) {
        std::cerr << "Cannot listen on port " << port << " (is it already in use?)\n";
        return 1;
    }
    std::cout << "Server running at http://localhost:" << port << "  (Ctrl+C to stop)\n";
    while (true) {
        sock_t c = accept(srv, nullptr, nullptr);
        if (c == BAD_SOCK) continue;
        std::thread([this, c]() { handleClient(c, handler); }).detach();
    }
}
