#!/usr/bin/env bash

# Code files must be utf-8 without BOM, otherwise it may cause build failure in some environments, such as pragma once in GCC 14.

has_utf8_bom() {
    local file=$1
    local prefix

    prefix=$(LC_ALL=C head -c 3 "$file" 2>/dev/null | od -An -tx1 2>/dev/null | tr -d '[:space:]')
    [[ "$prefix" == "efbbbf" ]]
}

while IFS= read -r -d '' f; do
    case "$f" in
        *.py | *.js | *.ts | *.go | *.rs | *.cpp | *.cxx | *.cc | *.c | *.h | *.hpp | *.java | *.sh | .gitattributes | .gitignore | .gitmodules )
            if [[ -f "$f" ]] && has_utf8_bom "$f"; then
                echo "ERROR: $f has UTF-8 BOM. Please save as UTF-8 without BOM."
                exit 1
            fi
            ;;
    esac
done < <(git diff --cached --name-only --diff-filter=ACMR -z)
