#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <cmath>
#include <cstdlib>
#include "Database.h"
#include "Server.h"
#include "Json.h"

static std::unique_ptr<Database> db;
static std::mutex mtx;                       // one request touches the DB at a time

static Response json(int st, const std::string& b) { return {st, "application/json; charset=utf-8", b}; }
static Response fail(int st, const std::string& m) { return json(st, "{\"error\":" + jstr(m) + "}"); }

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

struct ProductIn { std::string name; double price; int qty; };

static ProductIn readProduct(const std::string& body) {
    JVal v = parseJson(body);
    const JVal *n = v.get("name"), *p = v.get("price"), *q = v.get("qty");
    if (!n || n->t != JVal::Str || !p || p->t != JVal::Num || !q || q->t != JVal::Num)
        throw std::invalid_argument("name, price and qty are required");
    ProductIn r{trim(n->s), p->n, (int)q->n};
    if (r.name.empty() || r.name.size() > 100 || r.price < 0 || r.price > 1e9 ||
        q->n < 0 || q->n > 1e9 || q->n != std::floor(q->n))
        throw std::invalid_argument("Invalid product values");
    return r;
}

static Response api(const std::string& m, const std::string& p, const std::string& body) {
    if (p == "/api/products") {
        if (m == "GET") return json(200, db->productsJson());
        if (m == "POST") {
            ProductIn in = readProduct(body);
            int id = db->addProduct(in.name, in.price, in.qty);
            return json(201, "{\"id\":" + std::to_string(id) + "}");
        }
        return fail(405, "Method not allowed");
    }
    if (p.rfind("/api/products/", 0) == 0) {
        int id = std::atoi(p.c_str() + 14);
        if (id <= 0) return fail(400, "Invalid product id");
        if (m == "PUT") {
            ProductIn in = readProduct(body);
            return db->updateProduct(id, in.name, in.price, in.qty) ? json(200, "{\"ok\":true}") : fail(404, "Product not found");
        }
        if (m == "DELETE")
            return db->deleteProduct(id) ? json(200, "{\"ok\":true}") : fail(404, "Product not found");
        return fail(405, "Method not allowed");
    }
    if (p == "/api/bills") {
        if (m == "GET") return json(200, db->billsJson());
        if (m == "POST") {
            JVal v = parseJson(body);
            const JVal *c = v.get("customer"), *d = v.get("discount"), *its = v.get("items");
            if (!its || its->t != JVal::Arr) throw std::invalid_argument("items array required");
            std::vector<std::pair<int, int>> items;
            for (auto& it : its->a) {
                const JVal *id = it.get("id"), *q = it.get("qty");
                if (!id || !q || id->t != JVal::Num || q->t != JVal::Num) throw std::invalid_argument("Each item needs id and qty");
                items.push_back({(int)id->n, (int)q->n});
            }
            std::string err;
            std::string bill = db->createBill(c && c->t == JVal::Str ? trim(c->s).substr(0, 100) : "",
                                              d && d->t == JVal::Num ? d->n : 0, items, err);
            return bill.empty() ? fail(400, err) : json(201, bill);
        }
        return fail(405, "Method not allowed");
    }
    return fail(404, "Not found");
}

static Response serveStatic(std::string path) {
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos) return fail(400, "Bad path");
    std::ifstream f("web" + path, std::ios::binary);
    if (!f) return fail(404, "File not found");
    std::stringstream ss; ss << f.rdbuf();
    std::string type = "application/octet-stream";
    auto ends = [&](const char* e) { std::string s(e); return path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0; };
    if (ends(".html")) type = "text/html; charset=utf-8";
    else if (ends(".css")) type = "text/css; charset=utf-8";
    else if (ends(".js")) type = "application/javascript; charset=utf-8";
    else if (ends(".json")) type = "application/json";
    else if (ends(".svg")) type = "image/svg+xml";
    return {200, type, ss.str()};
}

static Response handle(const Request& rq) {
    std::lock_guard<std::mutex> lock(mtx);
    try {
        if (rq.path.rfind("/api/", 0) == 0) return api(rq.method, rq.path, rq.body);
        if (rq.method != "GET" && rq.method != "HEAD") return fail(405, "Method not allowed");
        return serveStatic(rq.path);
    } catch (const std::invalid_argument& e) {
        return fail(400, e.what());
    } catch (const std::exception& e) {
        return fail(500, e.what());
    }
}

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 8080;
    if (port <= 0 || port > 65535) { std::cerr << "Usage: inventory_server [port]\n"; return 1; }
    try {
        db.reset(new Database("inventory.db"));
    } catch (const std::exception& e) {
        std::cerr << "Database error: " << e.what() << "\n";
        return 1;
    }
    Server server(port, handle);
    return server.run();
}
