#!/usr/bin/env bash

SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";

CPPCHECK_OPTIONS="--relative-paths=$SCRIPT_DIR";
CPPCHECK_STD=c++11;
CPPCHECK_OUTPUT=cppcheck.result.xml;
CPPCHECK_ENABLE=all;

while getopts "ho:s:-" OPTION; do
    case $OPTION in
        h)
            echo "usage: $0 [options] [-- [cppcheck options...] ]";
            echo "options:";
            echo "-h                            help message.";
            echo "-o [output file]              output report of cppcheck(default: $CPPCHECK_OUTPUT)";
            echo "-s [cxx standard]             set cxx standard(default: $CPPCHECK_STD).";
            echo "-e [cppcheck rule]            --enable=OPTION of cppcheck(default: $CPPCHECK_ENABLE)";
            exit 0;
        ;;
        o)
            CPPCHECK_OUTPUT=$OPTARG;
        ;;
        e)
            CPPCHECK_ENABLE=$OPTARG;
        ;;
        s)
            CPPCHECK_STD=$OPTARG;
        ;;
        -)
            break;
        ;;
        ?)
            echo "unkonw argument detected";
            exit 1;
        ;;
    esac
done

shift $(($OPTIND - 1));

for inc_dir in $(find "$SCRIPT_DIR/third_party" -name include); do
    CPPCHECK_OPTIONS="$CPPCHECK_OPTIONS -I$inc_dir";
done

CPPCHECK_OPTIONS="$CPPCHECK_OPTIONS -I$SCRIPT_DIR/third_party/libiniloader/repo";

if [ ".xml" == "${CPPCHECK_OUTPUT:((${#CPPCHECK_OUTPUT}-4))}" ]; then
    CPPCHECK_OPTIONS="$CPPCHECK_OPTIONS --xml";
fi

cppcheck --enable=$CPPCHECK_ENABLE --std=$CPPCHECK_STD $CPPCHECK_OPTIONS --inconclusive "$SCRIPT_DIR/src" "$SCRIPT_DIR/atframework" "$@" 2>"$CPPCHECK_OUTPUT";
