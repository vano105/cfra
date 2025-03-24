## GPU-based matrices implementation of CFL algorithm

# Configure and build
git submodule update --init --recursive  \
cmake -B build -S . \
cmake --build build

# Run tests
./build/cfra
