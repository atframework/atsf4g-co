$PSDefaultParameterValues['*:Encoding'] = 'UTF-8'

$OutputEncoding = [System.Text.UTF8Encoding]::new()

$ALL_CMAKE_FILES = Get-ChildItem -Depth 0 -File -Include "*.cmake.in", "*.cmake", "CMakeLists.txt"
$ALL_CMAKE_FILES += Get-ChildItem -Depth 0 -Directory                                   `
    -Exclude ".vs", ".vscode", ".clion", "build_jobs_*", "3rd_party", "third_party", `
    "atframework/atframe_utils", `
    "atframework/libatbus", `
    "atframework/libatapp"                                                         `
| ForEach-Object -Process { Get-ChildItem -LiteralPath $_ -Recurse -File -Include "*.cmake.in", "*.cmake", "CMakeLists.txt" }

cmake-format -i $ALL_CMAKE_FILES
