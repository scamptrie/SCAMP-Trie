# SCAMP-Trie: Top-level Makefile
# Builds the SCAMP-Trie core and all baseline algorithms.
#
# Usage:
#   make              - Build SCAMP-Trie core
#   make baselines    - Build all C++ baseline algorithms
#   make all          - Build everything
#   make eval         - Run full evaluation (all algorithms, all datasets)
#   make clean        - Remove all build artifacts

CXX ?= g++
CXXFLAGS = -Wall -O3 -std=c++11
CC ?= gcc
CFLAGS = -Wall -O3 -std=c99

.PHONY: all scamptrie baselines clean eval help

all: scamptrie baselines

# --- SCAMP-Trie core ---
scamptrie:
	$(MAKE) -C src

# --- Baseline C++ algorithms ---
# Note: On Windows/MinGW, g++ appends .exe automatically.
# The @mv handles this by copying .exe to the expected name if needed.
random:
	@echo "Compiling random baseline..."
	$(CXX) $(CXXFLAGS) -o baselines/random/random baselines/random/random.cpp
	@test -f baselines/random/random.exe && mv baselines/random/random.exe baselines/random/random || true

olp:
	@echo "Compiling olp baseline..."
	$(CXX) $(CXXFLAGS) -o baselines/olp/olp baselines/olp/olp.cpp
	@test -f baselines/olp/olp.exe && mv baselines/olp/olp.exe baselines/olp/olp || true

biolp:
	@echo "Compiling biolp baseline..."
	$(CXX) $(CXXFLAGS) -o baselines/biolp/biolp baselines/biolp/biolp.cpp
	@test -f baselines/biolp/biolp.exe && mv baselines/biolp/biolp.exe baselines/biolp/biolp || true

treebitmap:
	@echo "Compiling treebitmap baseline..."
	$(CXX) $(CXXFLAGS) -o baselines/treebitmap/treebitmap baselines/treebitmap/treebitmap.cpp
	@test -f baselines/treebitmap/treebitmap.exe && mv baselines/treebitmap/treebitmap.exe baselines/treebitmap/treebitmap || true

pop1:
	@echo "Compiling pop1 baseline..."
	$(CXX) $(CXXFLAGS) -o baselines/pop1/pop1 baselines/pop1/pop1.cpp
	@test -f baselines/pop1/pop1.exe && mv baselines/pop1/pop1.exe baselines/pop1/pop1 || true

poptrie:
	@echo "Compiling poptrie baseline..."
	$(CC) $(CFLAGS) -o baselines/poptrie/poptrie baselines/poptrie/poptrie.c baselines/poptrie/poptrie6.c baselines/poptrie/poptrie_main.c baselines/poptrie/buddy.c -lm
	@test -f baselines/poptrie/poptrie.exe && mv baselines/poptrie/poptrie.exe baselines/poptrie/poptrie || true

baselines: random olp biolp treebitmap pop1 poptrie

# --- Evaluation ---
eval: all
	bash scripts/launch.sh

# --- Weight generation (simulated annealing) ---
weights:
	$(MAKE) -C src annealing-scamptrie

quick-test:
	$(MAKE) -C src annealing-test

# --- Cleanup ---
clean:
	$(MAKE) -C src clean
	rm -f baselines/random/random baselines/olp/olp baselines/biolp/biolp \
	      baselines/treebitmap/treebitmap baselines/pop1/pop1 baselines/poptrie/poptrie

help:
	@echo "SCAMP-Trie Build System"
	@echo "======================="
	@echo "make              Build SCAMP-Trie core"
	@echo "make baselines    Build all C++ baselines"
	@echo "make all          Build everything (core + baselines)"
	@echo "make eval         Run full evaluation on all datasets"
	@echo "make weights      Generate optimized weights via annealing"
	@echo "make clean        Remove all build artifacts"
	@echo "make help         Show this message"
	@echo ""
	@echo "For more details, see README.md"
