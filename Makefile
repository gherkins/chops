BUILD_DIR := build
APP := $(BUILD_DIR)/chops_artefacts/Release/Standalone/chops.app

# Workaround for broken CommandLineTools installs that are missing their
# libc++ headers: fall back to the copy inside the SDK. No-op on healthy
# machines. (JUCE spawns nested cmake configures during the build, so the
# flags are exported for every target, not just the initial configure.)
CLT := /Library/Developer/CommandLineTools
ifeq ($(wildcard $(CLT)/usr/include/c++/v1),)
ifneq ($(wildcard $(CLT)/SDKs/MacOSX.sdk/usr/include/c++/v1),)
export CXXFLAGS += -nostdinc++ -isystem $(CLT)/SDKs/MacOSX.sdk/usr/include/c++/v1
endif
endif

.PHONY: all configure build run test clean

all: build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

configure: $(BUILD_DIR)/CMakeCache.txt

build: configure
	cmake --build $(BUILD_DIR) --parallel

run: build
	open $(APP)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	rm -rf $(BUILD_DIR)
