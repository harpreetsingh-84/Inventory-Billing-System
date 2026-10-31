#include "Database.h"
#include "Json.h"
#include <sqlite3.h>
#include <stdexcept>
#include <map>
#include <cmath>
#include <cstdio>

static const double GST_RATE = 18.0;   // percent

namespace {
// RAII wrapper around a prepared statement
struct Stmt {
    sqlite3* db; sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* d, const char* sql) : db(d) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(s); }
    Stmt(const Stmt&) = delete;
    void text(int i, const std::string& v) { sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT); }
    void dbl(int i, double v) { sqlite3_bind_double(s, i, v); }
    void num(int i, int v) { sqlite3_bind_int(s, i, v); }
    bool row() {
        int r = sqlite3_step(s);
        if (r == SQLITE_ROW) return true;
        if (r == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    void run() { row(); }
    std::string col(int i) { const unsigned char* t = sqlite3_column_text(s, i); return t ? (const char*)t : ""; }
    double d(int i) { return sqlite3_column_double(s, i); }
    int n(int i) { return sqlite3_column_int(s, i); }
};
std::string money(double v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", v); return b; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }
}

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
        throw std::runtime_error("Cannot open database: " + path);
    exec("PRAGMA foreign_keys=ON;");
    exec("CREATE TABLE IF NOT EXISTS products("
         " id INTEGER PRIMARY KEY AUTOINCREMENT,"
         " name TEXT NOT NULL,"
         " price REAL NOT NULL CHECK(price>=0),"
         " qty INTEGER NOT NULL CHECK(qty>=0));"
         "CREATE TABLE IF NOT EXISTS bills("
         " id INTEGER PRIMARY KEY,"
         " date TEXT NOT NULL,"
         " customer TEXT NOT NULL,"
         " sub REAL, disc REAL, gst REAL, total REAL);"
         "CREATE TABLE IF NOT EXISTS bill_items("
         " bill_id INTEGER NOT NULL REFERENCES bills(id),"
         " product_id INTEGER, name TEXT, price REAL, qty INTEGER);");
    Stmt c(db, "SELECT COUNT(*) FROM products");
    c.row();
    if (c.n(0) == 0) {   // first run: sample data
        addProduct("Pen", 10, 100);
        addProduct("Notebook", 50, 30);
        addProduct("Stapler", 120, 4);
    }
}

Database::~Database() { if (db) sqlite3_close(db); }

void Database::exec(const char* sql) {
    char* msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
        std::string m = msg ? msg : "SQL error";
        sqlite3_free(msg);
        throw std::runtime_error(m);
    }
}

std::string Database::productsJson() {
    Stmt s(db, "SELECT id,name,price,qty FROM products ORDER BY id");
    std::string out = "[";
    bool first = true;
    while (s.row()) {
        if (!first) out += ",";
        first = false;
        out += "{\"id\":" + std::to_string(s.n(0)) + ",\"name\":" + jstr(s.col(1)) +
               ",\"price\":" + money(s.d(2)) + ",\"qty\":" + std::to_string(s.n(3)) + "}";
    }
    return out + "]";
}

int Database::addProduct(const std::string& name, double price, int qty) {
    Stmt s(db, "INSERT INTO products(name,price,qty) VALUES(?,?,?)");
    s.text(1, name); s.dbl(2, price); s.num(3, qty);
    s.run();
    return (int)sqlite3_last_insert_rowid(db);
}

bool Database::updateProduct(int id, const std::string& name, double price, int qty) {
    Stmt s(db, "UPDATE products SET name=?,price=?,qty=? WHERE id=?");
    s.text(1, name); s.dbl(2, price); s.num(3, qty); s.num(4, id);
    s.run();
    return sqlite3_changes(db) > 0;
}

bool Database::deleteProduct(int id) {
    Stmt s(db, "DELETE FROM products WHERE id=?");
    s.num(1, id);
    s.run();
    return sqlite3_changes(db) > 0;
}

std::string Database::createBill(const std::string& customer, double discountPct,
                                 const std::vector<std::pair<int, int>>& items, std::string& err) {
    if (items.empty()) { err = "Cart is empty"; return ""; }
    if (discountPct < 0 || discountPct > 100) { err = "Discount must be 0-100"; return ""; }
    std::map<int, int> want;                       // merge duplicate product ids
    for (auto& it : items) {
        if (it.second <= 0) { err = "Quantity must be at least 1"; return ""; }
        want[it.first] += it.second;
    }
    exec("BEGIN IMMEDIATE");
    try {
        struct Line { int id; std::string name; double price; int qty; };
        std::vector<Line> lines;
        double sub = 0;
        for (auto& w : want) {
            Stmt p(db, "SELECT name,price,qty FROM products WHERE id=?");
            p.num(1, w.first);
            if (!p.row()) throw std::invalid_argument("Product " + std::to_string(w.first) + " not found");
            if (w.second > p.n(2)) throw std::invalid_argument("Not enough stock for " + p.col(0));
            lines.push_back({w.first, p.col(0), p.d(1), w.second});
            sub += p.d(1) * w.second;
        }
        sub = round2(sub);
        double disc = round2(sub * discountPct / 100.0);
        double gst = round2((sub - disc) * GST_RATE / 100.0);
        double total = round2(sub - disc + gst);

        Stmt n(db, "SELECT COALESCE(MAX(id),1000)+1 FROM bills");
        n.row();
        int billNo = n.n(0);

        Stmt b(db, "INSERT INTO bills(id,date,customer,sub,disc,gst,total) "
                   "VALUES(?,datetime('now','localtime'),?,?,?,?,?)");
        b.num(1, billNo); b.text(2, customer.empty() ? "Walk-in" : customer);
        b.dbl(3, sub); b.dbl(4, disc); b.dbl(5, gst); b.dbl(6, total);
        b.run();

        for (auto& l : lines) {
            Stmt i(db, "INSERT INTO bill_items(bill_id,product_id,name,price,qty) VALUES(?,?,?,?,?)");
            i.num(1, billNo); i.num(2, l.id); i.text(3, l.name); i.dbl(4, l.price); i.num(5, l.qty);
            i.run();
            Stmt u(db, "UPDATE products SET qty=qty-? WHERE id=?");
            u.num(1, l.qty); u.num(2, l.id);
            u.run();
        }
        exec("COMMIT");
        return fetchBills(billNo);
    } catch (const std::invalid_argument& e) {
        exec("ROLLBACK"); err = e.what(); return "";
    } catch (...) {
        try { exec("ROLLBACK"); } catch (...) {}
        throw;
    }
}

std::string Database::fetchBills(int onlyId) {
    Stmt s(db, "SELECT id,date,customer,sub,disc,gst,total FROM bills "
               "WHERE (?1=0 OR id=?1) ORDER BY id DESC");
    s.num(1, onlyId);
    std::string out;
    bool first = true;
    while (s.row()) {
        if (!first) out += ",";
        first = false;
        int id = s.n(0);
        out += "{\"no\":" + std::to_string(id) + ",\"date\":" + jstr(s.col(1)) +
               ",\"customer\":" + jstr(s.col(2)) + ",\"sub\":" + money(s.d(3)) +
               ",\"disc\":" + money(s.d(4)) + ",\"gst\":" + money(s.d(5)) +
               ",\"total\":" + money(s.d(6)) + ",\"items\":[";
        Stmt it(db, "SELECT product_id,name,price,qty FROM bill_items WHERE bill_id=?");
        it.num(1, id);
        bool f2 = true;
        while (it.row()) {
            if (!f2) out += ",";
            f2 = false;
            out += "{\"id\":" + std::to_string(it.n(0)) + ",\"name\":" + jstr(it.col(1)) +
                   ",\"price\":" + money(it.d(2)) + ",\"qty\":" + std::to_string(it.n(3)) + "}";
        }
        out += "]}";
    }
    return out;
}

std::string Database::billsJson() { return "[" + fetchBills(0) + "]"; }
