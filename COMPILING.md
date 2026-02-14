# Building ssfhelper

## Prerequisites
- MinGW-w64 (32-bit)
- CMake (3.7 or higher)
- Git (optional)

## Build Steps

```bash
# Clone the repository
git clone https://github.com/Conkwer/ssfhelper.git
cd ssfhelper

# Copy your M3U support implementation
cp SSFHelper_m3u_support.cpp main.cpp

# Build the executable
mingw32-make clean
mingw32-make
```
