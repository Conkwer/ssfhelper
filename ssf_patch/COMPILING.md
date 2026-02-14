# Building ssf_patch

## Prerequisites
- MinGW-w64 (32-bit)
- CMake (3.7 or higher)
- Git (optional)

## Build Steps

```bash
# Clone the repository
git clone https://github.com/Conkwer/ssfhelper.git
cd ssfhelper/ssf_patch

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake -G "MinGW Makefiles" ..

# Build
mingw32-make```