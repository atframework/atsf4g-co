# Build (atsf4g-co)

This project uses **CMake (>= 3.24)** and requires a C++17 toolchain.

## Windows (MSVC + vcpkg example)

```bash
mkdir build_jobs_msvc
cd build_jobs_msvc

# Configure with vcpkg
cmake [SOURCE_PATH] -G "Visual Studio 17 2022" -A x64 \
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" \
  -DPROJECT_ENABLE_UNITTEST=YES \
  -DPROJECT_ENABLE_SAMPLE=YES \
  -DPROJECT_ENABLE_TOOLS=YES

# Build
cmake --build . --config Debug
cmake --build . --config RelWithDebInfo  # For production
```

Notes:

- This repo often uses a bundled toolset layout; output files are typically under `<BUILD_DIR>/publish/`.

## Unix/Linux/macOS

```bash
# Auto setup with cmake_dev.sh
bash cmake_dev.sh [options] ...

# Example: Enable OpenSSL, unit tests, samples
bash ./cmake_dev.sh -lus -- -DCRYPTO_USE_OPENSSL=YES

# Build
cd build_jobs_*
cmake --build . -- -j4
```

## Key CMake Options

| Option                    | Default | Description                |
| ------------------------- | ------- | -------------------------- |
| `PROJECT_ENABLE_UNITTEST` | NO      | Enable unit tests          |
| `PROJECT_ENABLE_SAMPLE`   | NO      | Enable sample applications |
| `PROJECT_ENABLE_TOOLS`    | NO      | Enable tools               |
| `BUILD_SHARED_LIBS`       | NO      | Build shared libraries     |
| `CRYPTO_USE_OPENSSL`      | NO      | Use OpenSSL for crypto     |
| `CRYPTO_USE_MBEDTLS`      | NO      | Use MbedTLS for crypto     |

If you are unsure what was configured, inspect `CMakeCache.txt` in the build folder.
