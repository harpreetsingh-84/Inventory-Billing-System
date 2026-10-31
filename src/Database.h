#ifndef DATABASE_H
#define DATABASE_H
#include <string>
#include <vector>
#include <utility>

struct sqlite3;

// SQLite-backed storage. All query results are returned as JSON text.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::string productsJson();
    int  addProduct(const std::string& name, double price, int qty);
    bool updateProduct(int id, const std::string& name, double price, int qty);
    bool deleteProduct(int id);

    // items = (productId, quantity). Returns bill JSON, or "" and sets err.
    std::string createBill(const std::string& customer, double discountPct,
                           const std::vector<std::pair<int, int>>& items, std::string& err);
    std::string billsJson();

private:
    sqlite3* db = nullptr;
    void exec(const char* sql);
    std::string fetchBills(int onlyId);
};
#endif
