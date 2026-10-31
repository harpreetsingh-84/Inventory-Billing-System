CXX      = g++
CC       = gcc
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
SRC      = src/main.cpp src/Database.cpp src/Server.cpp
TARGET   = inventory_server
LIBS     =
OBJ      =

ifeq ($(OS),Windows_NT)
  LIBS += -lws2_32
  TARGET = inventory_server.exe
else
  LIBS += -ldl
endif

# If you placed sqlite3.c / sqlite3.h in src/, SQLite is compiled in (no install needed).
ifneq ($(wildcard src/sqlite3.c),)
  OBJ = sqlite3.o
else
  LIBS += -lsqlite3
endif

all: $(TARGET)

sqlite3.o: src/sqlite3.c
	$(CC) -O2 -c src/sqlite3.c -o sqlite3.o

$(TARGET): $(SRC) $(OBJ) src/Json.h src/Database.h src/Server.h
	$(CXX) $(CXXFLAGS) $(SRC) $(OBJ) -o $(TARGET) $(LIBS)

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET) inventory_server.exe sqlite3.o
