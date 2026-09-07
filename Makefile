.PHONY: help build test test-cpp test-noprov test-js test-oracle test-python test-readme clean rebuild format format-check lint wasm wasm-clean serve demo

BUILD_DIR := build
NOPROV_BUILD_DIR := build-noprov
WASM_BUILD_DIR := build-wasm
CLANG_FORMAT ?= clang-format
PYTHON ?= python3

.DEFAULT_GOAL := build

help:
	@echo "midi-sketch Build System"
	@echo ""
	@echo "  make build     - Build the project"
	@echo "  make test      - Run C++, WASM/JS, oracle, Python, and README example tests"
	@echo "  make test-noprov - Run C++ tests in the same shape the WASM module ships in"
	@echo "  make clean     - Clean build"
	@echo "  make rebuild   - Clean and rebuild"
	@echo "  make format    - Format code (C++ + js/ TS bindings)"
	@echo "  make format-check - Check formatting without writing"
	@echo "  make lint      - Run static checks without modifying files"
	@echo "  make wasm      - Build WASM module"
	@echo "  make wasm-clean- Clean WASM build"
	@echo "  make serve     - Start demo server (no build)"
	@echo "  make demo      - Build WASM and start demo server"

configure:
	@mkdir -p $(BUILD_DIR)
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DIR) --parallel

test: test-cpp test-js test-oracle test-python test-readme

test-cpp: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

# The shape the WASM module ships in: no NoteEvent provenance fields. Kept as a
# separate build tree because the definition changes NoteEvent's layout, so the
# two shapes cannot share object files. Not part of `make test` -- it doubles
# the C++ run for a difference only a handful of tests can see.
test-noprov:
	@mkdir -p $(NOPROV_BUILD_DIR)
	@cmake -B $(NOPROV_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DMIDISKETCH_NO_PROVENANCE=ON
	cmake --build $(NOPROV_BUILD_DIR) --parallel
	ctest --test-dir $(NOPROV_BUILD_DIR) --output-on-failure

test-js: wasm
	yarn test

test-oracle: build
	yarn test:oracle

test-python:
	$(PYTHON) -m unittest discover -s scripts/tests -p 'test_*.py'

test-readme: build wasm
	$(PYTHON) scripts/check_readme_examples.py

clean:
	rm -rf $(BUILD_DIR) $(NOPROV_BUILD_DIR)

rebuild: clean build

format:
	@find src tests -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) | xargs $(CLANG_FORMAT) -i
	yarn lint:fix
	$(MAKE) lint

format-check:
	@find src tests -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) | xargs $(CLANG_FORMAT) --dry-run --Werror
	$(MAKE) lint

lint:
	yarn lint

wasm-configure:
	emcmake cmake -B $(WASM_BUILD_DIR) -DBUILD_WASM=ON -DCMAKE_BUILD_TYPE=Release

wasm: wasm-configure
	cmake --build $(WASM_BUILD_DIR) --parallel
	@ls -lh dist/*.wasm dist/*.js 2>/dev/null || echo "WASM files not found"
	yarn build:js

wasm-clean:
	rm -rf $(WASM_BUILD_DIR)
	rm -rf dist/*.wasm dist/*.js

serve:
	@echo "Starting demo server at http://localhost:8080/demo/"
	@echo "Press Ctrl+C to stop"
	python3 -m http.server 8080

demo: wasm serve
