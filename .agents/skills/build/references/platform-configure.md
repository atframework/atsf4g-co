# Platform configure examples

Read this file only when creating a new build tree or selecting project feature options. Resolve `<BUILD_DIR>`, the
generator, parallelism, and existing explicit options from `.vscode/settings.json` or the user's command first.

## Windows with MSVC and vcpkg

```powershell
cmake -S . -B <BUILD_DIR> -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=<VCPKG_INSTALL_DIR>/scripts/buildsystems/vcpkg.cmake" `
  -DPROJECT_ENABLE_UNITTEST=YES `
  -DPROJECT_ENABLE_SAMPLE=YES `
  -DPROJECT_ENABLE_TOOLS=YES

cmake --build <BUILD_DIR> --config Debug
```

Use the live workspace generator instead when it differs. Multi-config generators need `--config`; Ninja normally
uses the configured `CMAKE_BUILD_TYPE`.

## Unix, Linux, or macOS

The repository helper can configure common dependency and feature combinations:

```bash
bash ./cmake_dev.sh -lus -- -DCRYPTO_USE_OPENSSL=YES
cmake --build <BUILD_DIR> --parallel <jobs>
```

Inspect `cmake_dev.sh` before changing its flags. Common project options include `PROJECT_ENABLE_UNITTEST`,
`PROJECT_ENABLE_SAMPLE`, `PROJECT_ENABLE_TOOLS`, `BUILD_SHARED_LIBS`, `CRYPTO_USE_OPENSSL`, and
`CRYPTO_USE_MBEDTLS`; confirm current defaults in CMake sources or `<BUILD_DIR>/CMakeCache.txt`.
