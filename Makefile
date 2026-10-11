.PHONY: all build test clean lint format security

all: build

build:
	cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel

test:
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build

lint:
	clang-tidy $(find domain application infrastructure -name '*.cpp')

format:
	clang-format -i $(find domain application infrastructure -name '*.cpp' -o -name '*.hpp')

security:
	trivy fs --severity HIGH,CRITICAL .
