BUILD_DIR ?= build
MKDIR_P ?= mkdir -p
MAKE_BUILD := $(shell $(MKDIR_P) $(BUILD_DIR))

getSrc  = $(shell find $(1) -name $(2))
getInc  = $(shell find $(1) -type d)

SRCS-s := $(call getSrc, src,   "*.c*")
SRCS-scenarios := $(SRCS-s) main.cpp

OBJS-scenarios   := $(SRCS-scenarios:%=$(BUILD_DIR)/%.o)

INC-total := $(call getInc, src)

STD ?= 14
CC     = gcc
CXX    = g++ -std=c++$(STD)
CFLAGS = -std=c++$(STD) -Wformat=0 -Wno-unused-command-line-argument -Wint-to-pointer-cast
LTO    = -flto

# target
TGT-scenarios := $(BUILD_DIR)/scenarios.app

data: INC_FLAGS  = $(addprefix -I,$(TGT-scenarios))
data: LINK_FLAGS += $(LTO)
data: CFLAGS += -O3 -g
data: LD_FLAGS += -lpthread
data: $(TGT-scenarios)

debug: INC_FLAGS  = $(addprefix -I,$(TGT-scenarios))
debug: CFLAGS += -DDEBUG -g3 -O0
debug: LINK_FLAGS += $(LTO)
debug: LD_FLAGS += -lpthread
debug: $(TGT-scenarios)

$(TGT-scenarios): $(OBJS-scenarios)
	@echo -n "[Create] $(notdir $@) with $(CXX) ... "
	@$(CXX) $(LINK_FLAGS) $(OBJS-scenarios) -o $@ $(LD_FLAGS)
	@echo "done!"
	@echo "[All done]"

# C++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	@$(MKDIR_P) $(dir $@)
	@echo -n "[Build]  $(notdir $@) with $(CXX) ... "
	@$(CXX) $(CFLAGS) -c $< -o $@ $(LD_FLAGS)
	@echo "done!"

# C source
$(BUILD_DIR)/%.c.o: %.c
	@$(MKDIR_P) $(dir $@)
	@echo -n "[Build]  $(notdir $@) with $(CC) ... "
	@$(CC) $(CFLAGS) -c $< -o $@ $(LD_FLAGS)
	@echo "done!"

.PHONY : clean
clean :
	rm -r $(BUILD_DIR)