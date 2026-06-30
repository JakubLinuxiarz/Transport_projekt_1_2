CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
LDFLAGS := -lsfml-graphics -lsfml-window -lsfml-system

SRC := $(wildcard src/*.cpp)
OUT := bin/krk_router

all: $(OUT)

$(OUT): $(SRC)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

clean:
	rm -rf bin
