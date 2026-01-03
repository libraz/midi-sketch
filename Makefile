.PHONY: help build test clean rebuild format wasm wasm-clean demo

BUILD_DIR := build
WASM_BUILD_DIR := build-wasm
CLANG_FORMAT ?= clang-format

.DEFAULT_GOAL := build

help:
	@echo "midi-sketch Build System"
	@echo ""
	@echo "  make build     - Build the project"
	@echo "  make test      - Run tests"
	@echo "  make clean     - Clean build"
	@echo "  make rebuild   - Clean and rebuild"
	@echo "  make format    - Format code"
	@echo "  make wasm      - Build WASM module"
	@echo "  make wasm-clean- Clean WASM build"
	@echo "  make demo      - Start demo server"

configure:
	@mkdir -p $(BUILD_DIR)
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean build

format:
	@find src tests -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) | xargs $(CLANG_FORMAT) -i

wasm-configure:
	emcmake cmake -B $(WASM_BUILD_DIR) -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release

wasm: wasm-configure
	cmake --build $(WASM_BUILD_DIR) --parallel
	@ls -lh dist/*.wasm dist/*.js 2>/dev/null || echo "WASM files not found"

wasm-clean:
	rm -rf $(WASM_BUILD_DIR)
	rm -rf dist/*.wasm dist/*.js

demo: wasm
	@echo "Starting demo server at http://localhost:8080/demo/"
	python3 -m http.server 8080
