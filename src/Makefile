# SCAMP-Trie — Core build
# Builds the `scamptrie` binary with MOO/annealing support.

CXX ?= g++
ANNEAL_MAX_ROUND ?= 300

CXXFLAGS = -std=c++14 -m64 -mpopcnt -O3 -DNDEBUG -march=native -mtune=native -fopenmp \
	-DDYN_ANNEAL_MAX_ROUND=$(ANNEAL_MAX_ROUND) \
	-I. -Wno-format

DEPFLAGS = -MT $@ -MMD -MP -MF $(@:.o=.d)
LDFLAGS = -lpthread -fopenmp

BUILD_DIR := build
TARGET := scamptrie

SRC_CPP := $(wildcard *.cpp) $(wildcard moo/*.cpp) $(wildcard utils/*.cpp)
OBJS := $(SRC_CPP:%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean rebuild debug profile \
	annealing-scamptrie annealing-test annealing-fset1-100 \
	gen-1 gen-2 eval-t0 eval-t1 eval-t2 eval-t3 eval-t4 eval-all \
	scamptrie-basic

all: $(BUILD_DIR) $(TARGET)

$(TARGET): $(BUILD_DIR) $(OBJS)
	@echo -n "[Link] $(TARGET) ... "
	@$(CXX) $(OBJS) -o $@ $(LDFLAGS)
	@echo done

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo -n "[Build] $(notdir $@) ... "
	@$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@
	@echo done

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

rebuild: clean all

debug: CXXFLAGS += -g -DDEBUG
debug: clean $(TARGET)

profile: CXXFLAGS += -pg
profile: LDFLAGS += -pg
profile: clean $(TARGET)

-include $(OBJS:.o=.d)

# --------- Datasets & Convenience Targets ---------
EVAL_DIR := ../data
WEIGHTS_DIR := ../weights
RATIO_DIR := ../ratio
FSET1 := rrc00_v6.fib isp1_kab.fib AWS_256k_v6.fib host-rd64.fib
FSET2 := rrc00_2021_v6.fib isp1_kab.fib AWS_256k_v6.fib host-rd64.fib

gen-1: $(TARGET)
	./$(TARGET) --gen $(foreach f,$(FSET1),-F $(EVAL_DIR)/$(f)) -W $(WEIGHTS_DIR)/weights_dyn.txt

gen-2: $(TARGET)
	./$(TARGET) --gen $(foreach f,$(FSET2),-F $(EVAL_DIR)/$(f)) -W $(WEIGHTS_DIR)/weights_dyn.txt

# Simulated annealing → ratio/scamptrie (ANNEAL_MAX_ROUND default 300)
annealing-scamptrie: $(TARGET)
	@mkdir -p $(RATIO_DIR) ../results/summary results/summary
	OMP_NUM_THREADS=$${OMP_NUM_THREADS:-1} ./$(TARGET) --gen \
		$(foreach f,$(FSET1),-F $(EVAL_DIR)/$(f)) -W $(RATIO_DIR)/scamptrie

# 100-round quick test on sample1.fib
annealing-test:
	rm -f $(BUILD_DIR)/moo/dyn_annealing.o
	@$(MAKE) ANNEAL_MAX_ROUND=100 $(TARGET)
	@mkdir -p $(RATIO_DIR) ../results/summary results/summary
	OMP_NUM_THREADS=$${OMP_NUM_THREADS:-1} ./$(TARGET) --gen \
		-F $(EVAL_DIR)/sample1.fib -W $(RATIO_DIR)/scamptrie-test100
	@echo "Done: weights -> $(RATIO_DIR)/scamptrie-test100"

# FSET1 full training set × 100-round annealing
annealing-fset1-100:
	rm -f $(BUILD_DIR)/moo/dyn_annealing.o
	@$(MAKE) ANNEAL_MAX_ROUND=100 $(TARGET)
	@mkdir -p $(RATIO_DIR) ../results/summary ../results results/summary
	@echo "=== annealing FSET1 r100 $$(date -Iseconds) ===" | tee ../results/annealing_fset1_r100.log
	OMP_NUM_THREADS=$${OMP_NUM_THREADS:-1} /usr/bin/time -p -o ../results/annealing_fset1_r100.time \
		./$(TARGET) --gen $(foreach f,$(FSET1),-F $(EVAL_DIR)/$(f)) \
		-W $(RATIO_DIR)/scamptrie_fset1_r100 >> ../results/annealing_fset1_r100.log 2>&1
	@echo "--- /usr/bin/time (-p fields in ../results/annealing_fset1_r100.time) ---" && cat ../results/annealing_fset1_r100.time

# SCAMP-Trie-Basic: same code with hash and free-child features disabled
scamptrie-basic: CXXFLAGS += -DDISABLE_SCAMP_HASH -DDISABLE_SCAMP_FREE_CHILD
scamptrie-basic: clean $(TARGET)
	@echo "Built SCAMP-Trie-Basic (hash/free-child disabled)"

eval-t0: $(TARGET)
	./$(TARGET) --eval -F $(EVAL_DIR)/rrc00_v6.fib -W $(WEIGHTS_DIR)/weights_default.txt
eval-t1: $(TARGET)
	./$(TARGET) --eval -F $(EVAL_DIR)/isp1_kab.fib -W $(WEIGHTS_DIR)/weights_default.txt
eval-t2: $(TARGET)
	./$(TARGET) --eval -F $(EVAL_DIR)/AWS_256k_v6.fib -W $(WEIGHTS_DIR)/weights_default.txt
eval-t3: $(TARGET)
	./$(TARGET) --eval -F $(EVAL_DIR)/host-rd64.fib -W $(WEIGHTS_DIR)/weights_default.txt
eval-t4: $(TARGET)
	./$(TARGET) --eval $(foreach f,$(FSET1),-F $(EVAL_DIR)/$(f)) -W $(WEIGHTS_DIR)/weights_default.txt

eval-all: $(TARGET)
	@F="$$(find $(EVAL_DIR) -maxdepth 1 -name '*.fib' | sort)"; \
	test -n "$$F" || (echo "No .fib in $(EVAL_DIR)"; exit 1); \
	args=$$(for p in $$F; do echo -n "-F $$p "; done); \
	./$(TARGET) --eval $$args -W $(WEIGHTS_DIR)/weights_default.txt
