CXX := g++

CXXFLAGS := -std=c++20 -O3 -march=native -Wall -Wextra -pthread
LDFLAGS := -pthread

SRC := src
BIN := bin

COMMON_SRC := \
	$(SRC)/graph.cpp

PREPROCESS_SRC := \
	$(SRC)/preprocess_osm.cpp \
	$(COMMON_SRC)

ROUTER_SRC := \
	$(SRC)/main.cpp \
	$(SRC)/monothread_djikstra.cpp \
	$(SRC)/monothread_Astar.cpp \
	$(SRC)/multithread_djikstra.cpp \
	$(SRC)/multithread_Astar.cpp \
	$(COMMON_SRC)

VISUALIZER_SRC := \
	$(SRC)/visualizer.cpp \
	$(SRC)/monothread_djikstra.cpp \
	$(COMMON_SRC)

all: dirs $(BIN)/preprocess $(BIN)/router $(BIN)/visualizer

dirs:
	mkdir -p $(BIN)

$(BIN)/preprocess: $(PREPROCESS_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lz -lbz2 -lexpat $(LDFLAGS)

$(BIN)/router: $(ROUTER_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN)/visualizer: $(VISUALIZER_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ \
		-lsfml-graphics \
		-lsfml-window \
		-lsfml-system \
		$(LDFLAGS)

clean:
	rm -rf $(BIN)

rebuild: clean all

.PHONY: all dirs clean rebuild