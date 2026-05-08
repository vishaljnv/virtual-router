# Makefile for vrouter

CXX      := g++
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wpedantic
OPT      := -O2
INCLUDES := -Iinclude -Ithird_party
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(INCLUDES)
LDFLAGS  := -pthread

TARGET   := vrouter
SRCDIR   := src
BUILDDIR := build

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(SRCS:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.o)
DEPS := $(OBJS:.o=.d)

# ---- Tests ------------------------------------------------------------------

TEST_TARGET    := vrouter_tests
TEST_BUILDDIR  := build/test

TEST_SRCS_PROD := $(filter-out $(SRCDIR)/main.cpp,$(SRCS))
TEST_SRCS_UNIT := $(wildcard test/unit/*.cpp)

TEST_OBJS := \
    $(TEST_SRCS_PROD:$(SRCDIR)/%.cpp=$(TEST_BUILDDIR)/%.o) \
    $(TEST_SRCS_UNIT:test/unit/%.cpp=$(TEST_BUILDDIR)/unit_%.o)
TEST_DEPS := $(TEST_OBJS:.o=.d)

# ---- Targets ----------------------------------------------------------------

.PHONY: all clean debug test integration-test

all: $(TARGET)

debug: OPT      := -O0 -g
debug: CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(INCLUDES)
debug: clean $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Tests

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(TEST_BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(TEST_BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILDDIR)/unit_%.o: test/unit/%.cpp | $(TEST_BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST_BUILDDIR):
	mkdir -p $(TEST_BUILDDIR)

integration-test: $(TARGET)
	./test/integration.sh

-include $(DEPS) $(TEST_DEPS)

clean:
	rm -rf build $(TARGET) $(TEST_TARGET)
