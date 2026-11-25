# Minimal build when CMake is not on PATH (MinGW mingw32-make).
# Verified on: MinGW.org GCC 6.3.0, GNU Make 3.82.

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -Wall -Wextra -pedantic
LDFLAGS   ?=

BINDIR    := build
TARGET    := $(BINDIR)/satrar.exe
SOURCES   := app/main.cpp

.PHONY: all clean run help

all: $(TARGET)

$(TARGET): $(SOURCES) | $(BINDIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

$(BINDIR):
	mkdir -p $(BINDIR)

run: $(TARGET)
	./$(TARGET) --help

# Works with cmd-style rmdir (Windows MinGW); use `rm -rf` on Unix make.
clean:
	-if exist "$(BINDIR)" rmdir /s /q "$(BINDIR)"

help:
	@echo "Targets: all (default), run, clean"