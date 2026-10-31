# Inventory & Billing System - C++ Server + SQLite + Web UI

A C++17 web application: the C++ program is an HTTP server that stores everything in a
SQLite database (`inventory.db`) and serves the HTML interface in `web/`.

    Browser (web/index.html)  --JSON/HTTP-->  C++ server (src/)  -->  SQLite (inventory.db)

## Features
- Products: add, edit, delete, search, low-stock alerts (stored in SQLite)
- Billing: cart, discount %, 18% GST - calculated on the SERVER from database prices
- Bills saved with line items in the database; stock is deducted inside one SQL transaction
  (all-or-nothing, so stock can never go negative)
- Sales report, reopen/print any past invoice
- Dashboard with stock value and revenue

## 1. Install SQLite
**Windows (easiest, nothing to install):**
1. Download "sqlite-amalgamation" ZIP from https://www.sqlite.org/download.html
2. Copy `sqlite3.c` and `sqlite3.h` into the `src/` folder. The Makefile compiles it in automatically.

**Ubuntu/Debian:** `sudo apt install libsqlite3-dev`
**macOS:** `brew install sqlite` (usually preinstalled)
**Windows MSYS2/MinGW alternative:** `pacman -S mingw-w64-x86_64-sqlite3`

## 2. Build and run
    make
    ./inventory_server          (Windows: inventory_server.exe)

Without make (Linux/macOS):
    g++ -std=c++17 -pthread src/main.cpp src/Database.cpp src/Server.cpp -lsqlite3 -o inventory_server

Without make (Windows, with sqlite3.c in src/):
    gcc -c src/sqlite3.c -o sqlite3.o
    g++ -std=c++17 src/main.cpp src/Database.cpp src/Server.cpp sqlite3.o -o inventory_server.exe -lws2_32

Then open **http://localhost:8080** in your browser. Optional port: `./inventory_server 9000`.
Run it from the project folder (it needs `web/` and creates `inventory.db` there).

## REST API
| Method | URL | Purpose |
|--------|-----|---------|
| GET | /api/products | list products |
| POST | /api/products | add `{name, price, qty}` |
| PUT | /api/products/{id} | update product |
| DELETE | /api/products/{id} | delete product |
| GET | /api/bills | all bills with items |
| POST | /api/bills | create bill `{customer, discount, items:[{id, qty}]}` |

## Database schema
- products(id, name, price, qty)
- bills(id, date, customer, sub, disc, gst, total)
- bill_items(bill_id, product_id, name, price, qty)

## Structure
    src/main.cpp      routing: REST API + static files
    src/Server.*      HTTP server on raw sockets (Winsock / POSIX), one thread per request
    src/Database.*    all SQL: prepared statements, transactions
    src/Json.h        small JSON parser/escaper
    web/index.html    frontend (fetch() calls to the API)
    console_version/  earlier console-only version (file based)

## Viva points
Sockets and HTTP protocol, REST design, multithreading with a mutex, SQL transactions (ACID),
prepared statements (prevents SQL injection), server-side validation, RAII wrappers,
separation of layers (UI / server / database).
