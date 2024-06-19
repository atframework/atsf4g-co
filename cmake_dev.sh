#!/bin/bash
#!/usr/bin/env bash

SYS_NAME="$(uname -s)"
SYS_NAME="$(basename $SYS_NAME)"
CC=gcc
CXX=g++
CCACHE="$(which ccache)"
DISTCC=""
if [[ ! -z "$DISTCC_HOSTS" ]]; then
  DISTCC="$(which distcc 2>/dev/null)"
fi

NINJA_BIN="$(which ninja 2>&1)"
if [[ $? -ne 0 ]]; then
  NINJA_BIN="$(which ninja-build 2>&1)"
  if [[ $? -ne 0 ]]; then
    NINJA_BIN=""
  fi
fi

CMAKE_OPTIONS=""
CMAKE_CLANG_TIDY=""
CMAKE_CLANG_ANALYZER=0
CMAKE_CLANG_ANALYZER_PATH=""
BUILD_DIR=$(echo "build_jobs_$SYS_NAME" | tr '[:upper:]' '[:lower:]')
BUILD_DIR_SET=0
CMAKE_BUILD_TYPE=Debug
PROJECT_NAME="$(basename "$0")"

which git >/dev/null 2>&1
if [[ $? -eq 0 ]]; then
  PROJECT_GIT_URL=$(git config remote.origin.url 2>/dev/null)
  if [[ "x$PROJECT_GIT_URL" != "x" ]]; then
    PROJECT_NAME="${PROJECT_GIT_URL##*/}"
    PROJECT_NAME="${PROJECT_NAME//\.git/}"
  fi
fi

if [[ ! -z "$MSYSTEM" ]]; then
  CHECK_MSYS=$(echo "${MSYSTEM:0:5}" | tr '[:upper:]' '[:lower:]')
else
  CHECK_MSYS=""
fi

while getopts "ab:c:d:e:hln:tur:s-" OPTION; do
  case $OPTION in
    a)
      echo "Ready to check ccc-analyzer and c++-analyzer, please do not use -c to change the compiler when using clang-analyzer."
      export CCC_CC="$CC"
      export CCC_CXX="$CXX"
      CC=$(which ccc-analyzer)
      CXX=$(which c++-analyzer)
      if [[ 0 -ne $? ]]; then
        # check mingw path
        if [[ "mingw" == "$CHECK_MSYS" ]]; then
          if [[ ! -z "$MINGW_MOUNT_POINT" ]] && [[ -e "$MINGW_MOUNT_POINT/libexec/ccc-analyzer.bat" ]] && [[ -e "$MINGW_MOUNT_POINT/libexec/ccc-analyzer.bat" ]]; then
            echo "clang-analyzer found in $MINGW_MOUNT_POINT"
            export PATH=$PATH:$MINGW_MOUNT_POINT/libexec
            CC="$MINGW_MOUNT_POINT/libexec/ccc-analyzer.bat"
            CXX="$MINGW_MOUNT_POINT/libexec/ccc-analyzer.bat"
            CMAKE_CLANG_ANALYZER_PATH="$MINGW_MOUNT_POINT/libexec"
          elif [[ ! -z "$MINGW_PREFIX" ]] && [[ -e "$MINGW_PREFIX/libexec/ccc-analyzer.bat" ]] && [[ -e "$MINGW_PREFIX/libexec/c++-analyzer.bat" ]]; then
            echo "clang-analyzer found in $MINGW_PREFIX"
            export PATH=$PATH:$MINGW_PREFIX/libexec
            CC="$MINGW_PREFIX/libexec/ccc-analyzer.bat"
            CXX="$MINGW_PREFIX/libexec/ccc-analyzer.bat"
            CMAKE_CLANG_ANALYZER_PATH="$MINGW_PREFIX/libexec"
          fi
        else
          TEST_CLANG_BIN="$(which clang 2>/dev/null)"
          if [[ ! -z "$TEST_CLANG_BIN" ]]; then
            TEST_CLANG_BIN="$(readlink -f "$TEST_CLANG_BIN")"
            TEST_CLANG_BIN_DIR="$(dirname "$TEST_CLANG_BIN")"
            if [[ -e "$TEST_CLANG_BIN_DIR/../libexec/ccc-analyzer" ]]; then
              CC=$(readlink -f "$TEST_CLANG_BIN_DIR/../libexec/ccc-analyzer")
            fi
            if [[ -e "$TEST_CLANG_BIN_DIR/../libexec/c++-analyzer" ]]; then
              CXX=$(readlink -f "$TEST_CLANG_BIN_DIR/../libexec/c++-analyzer")
            fi
          fi
        fi
      fi

      if [[ -z "$CC" ]] || [[ -z "$CXX" ]]; then
        echo "ccc-analyzer=$CC"
        echo "c++-analyzer=$CXX"
        echo "clang-analyzer not found, failed."
        exit 1
      fi
      echo "ccc-analyzer=$CC"
      echo "c++-analyzer=$CXX"
      echo "clang-analyzer setup completed."
      CMAKE_CLANG_ANALYZER=1
      if [[ $BUILD_DIR_SET -eq 0 ]]; then
        BUILD_DIR="${BUILD_DIR}_analyzer"
      fi
      ;;
    b)
      CMAKE_BUILD_TYPE="$OPTARG"
      ;;
    c)
      if [[ $CMAKE_CLANG_ANALYZER -ne 0 ]]; then
        CCC_CC="$OPTARG"
        CCC_CXX="${CCC_CC/%clang/clang++}"
        CCC_CXX="${CCC_CC/%clang++-cl/clang-cl}"
        CCC_CXX="${CCC_CXX/%gcc/g++}"
        export CCC_CC
        export CCC_CXX
      else
        CC="$OPTARG"
        CXX="$(echo "$CC" | sed 's/\(.*\)clang/\1clang++/')"
        CXX="$(echo "$CXX" | sed 's/\(.*\)gcc/\1g++/')"
      fi
      ;;
    d)
      DISTCC="$OPTARG"
      ;;
    e)
      CCACHE="$OPTARG"
      ;;
    h)
      echo "usage: $0 [options] [-- [cmake options...] ]"
      echo "options:"
      echo "-a                            using clang-analyzer."
      echo "-c <compiler>                 compiler toolchains(gcc, clang or others)."
      echo "-d <distcc path>              try to use specify distcc to speed up building."
      echo "-e <ccache path>              try to use specify ccache to speed up building."
      echo "-h                            help message."
      echo "-n <sanitizer>                using sanitizer(address, thread, leak, hwaddress or undefined)."
      echo "-t                            enable clang-tidy."
      echo "-u                            enable unit test."
      echo "-s                            enable sample."
      echo "-l                            enable tools."
      exit 0
      ;;
    n)
      if [[ "x$OPTARG" == "xaddress" ]]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_SANTIZER_USE_ADDRESS=YES"
        if [[ $BUILD_DIR_SET -eq 0 ]]; then
          BUILD_DIR="${BUILD_DIR}_asan"
        fi
      elif [[ "x$OPTARG" == "xthread" ]]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_SANTIZER_USE_THREAD=YES"
        if [[ $BUILD_DIR_SET -eq 0 ]]; then
          BUILD_DIR="${BUILD_DIR}_tsan"
        fi
      elif [[ "x$OPTARG" == "xleak" ]] || [[ "x$OPTARG" == "xlsan" ]]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_SANTIZER_USE_LEAK=YES"
        if [[ $BUILD_DIR_SET -eq 0 ]]; then
          BUILD_DIR="${BUILD_DIR}_lsan"
        fi
      elif [[ "x$OPTARG" == "xhwaddress" ]] || [[ "x$OPTARG" == "xhwasan" ]]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_SANTIZER_USE_HWADDRESS=YES"
        if [[ $BUILD_DIR_SET -eq 0 ]]; then
          BUILD_DIR="${BUILD_DIR}_hwasan"
        fi
      elif [[ "x$OPTARG" == "xundefined" ]] || [[ "x$OPTARG" == "xubsan" ]]; then
        CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_SANTIZER_USE_UNDEFINED=YES"
        if [[ $BUILD_DIR_SET -eq 0 ]]; then
          BUILD_DIR="${BUILD_DIR}_ubsan"
        fi
      else
        echo "Only address, thread, leak, hwaddress and undefined sanitizers are available now"
        exit 1
      fi
      ;;
    t)
      CMAKE_CLANG_TIDY="-D -checks=* --"
      ;;
    u)
      CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_ENABLE_UNITTEST=YES"
      ;;
    r)
      BUILD_DIR="$OPTARG"
      BUILD_DIR_SET=1
      ;;
    s)
      CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_ENABLE_SAMPLE=YES"
      ;;
    l)
      CMAKE_OPTIONS="$CMAKE_OPTIONS -DPROJECT_ENABLE_TOOLS=YES"
      ;;
    -)
      break
      ;;
    ?)
      echo "unkonw argument detected"
      exit 1
      ;;
  esac
done

shift $(($OPTIND - 1))
SCRIPT_DIR="$(cd $(dirname $0) && pwd)"
mkdir -p "$SCRIPT_DIR/$BUILD_DIR"
cd "$SCRIPT_DIR/$BUILD_DIR"

# If use -stdlib=libstdc++ with clang
# if [[ $CMAKE_CLANG_ANALYZER -ne 0 ]]; then
#   export LDFLAGS="$LDFLAGS -lstdc++ -lc++abi -ldl -lm -lgcc_s"
#   echo "$CCC_CC" | grep clang >/dev/null 2>&1 && CMAKE_OPTIONS="$CMAKE_OPTIONS -DCOMPILER_OPTION_CLANG_ENABLE_LIBCXX=OFF -DCMAKE_CXX_FLAGS=-stdlib=libstdc++ -DCMAKE_CXX_STANDARD=20"
# else
#   echo "$CC" | grep clang >/dev/null 2>&1 && CMAKE_OPTIONS="$CMAKE_OPTIONS -DCOMPILER_OPTION_CLANG_ENABLE_LIBCXX=OFF -DCMAKE_CXX_FLAGS=-stdlib=libstdc++ -DCMAKE_CXX_STANDARD=20"
# fi

if [[ ! -z "$DISTCC" ]] && [[ "$DISTCC" != "disable" ]] && [[ "$DISTCC" != "disabled" ]] && [[ "$DISTCC" != "no" ]] && [[ "$DISTCC" != "false" ]] && [[ -e "$DISTCC" ]]; then
  CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_C_COMPILER_LAUNCHER=$DISTCC -DCMAKE_CXX_COMPILER_LAUNCHER=$DISTCC -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX"
elif [[ ! -z "$CCACHE" ]] && [[ "$CCACHE" != "disable" ]] && [[ "$CCACHE" != "disabled" ]] && [[ "$CCACHE" != "no" ]] && [[ "$CCACHE" != "false" ]] && [[ -e "$CCACHE" ]]; then
  #CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_C_COMPILER=$CCACHE -DCMAKE_CXX_COMPILER=$CCACHE -DCMAKE_C_COMPILER_ARG1=$CC -DCMAKE_CXX_COMPILER_ARG1=$CXX";
  CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_C_COMPILER_LAUNCHER=$CCACHE -DCMAKE_CXX_COMPILER_LAUNCHER=$CCACHE -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX"
else
  CMAKE_OPTIONS="$CMAKE_OPTIONS -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX"
fi

CMAKE_BIN=(cmake)

if [[ $CMAKE_CLANG_ANALYZER -ne 0 ]]; then
  CMAKE_BIN=(scan-build cmake)
fi

if [[ "x$NINJA_BIN" != "x" ]]; then
  ${CMAKE_BIN[@]} .. -G Ninja -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE $CMAKE_OPTIONS "$@"
elif [[ "$CHECK_MSYS" == "mingw" ]]; then
  ${CMAKE_BIN[@]} .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE $CMAKE_OPTIONS "$@"
else
  ${CMAKE_BIN[@]} .. -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE $CMAKE_OPTIONS "$@"
fi

if [[ 1 -eq $CMAKE_CLANG_ANALYZER ]]; then
  echo "========================================================================================================="
  CMAKE_CLANG_ANALYZER_OPTIONS="--exclude ../third_party --exclude ../src/server_frame/protocol -analyzer-config aggressive-binary-operation-simplification=true"
  if [[ -e "$SCRIPT_DIR/.scan-build.enable" ]]; then
    for OPT in $(cat "$SCRIPT_DIR/.scan-build.enable"); do
      CMAKE_CLANG_ANALYZER_OPTIONS="$CMAKE_CLANG_ANALYZER_OPTIONS -enable-checker $OPT"
    done
  fi

  if [[ -e "$SCRIPT_DIR/.scan-build.disable" ]]; then
    for OPT in $(cat "$SCRIPT_DIR/.scan-build.disable"); do
      CMAKE_CLANG_ANALYZER_OPTIONS="$CMAKE_CLANG_ANALYZER_OPTIONS -disable-checker $OPT"
    done
  fi

  for BUILD_JOBS_DIRS in ../build_*; do
    CMAKE_CLANG_ANALYZER_OPTIONS="$CMAKE_CLANG_ANALYZER_OPTIONS --exclude $BUILD_JOBS_DIRS"
  done

  for TEST_DIRS in $(find ../atframework -name test); do
    CMAKE_CLANG_ANALYZER_OPTIONS="$CMAKE_CLANG_ANALYZER_OPTIONS --exclude $TEST_DIRS"
  done
  CMAKE_CLANG_ANALYZER_OPTIONS="$CMAKE_CLANG_ANALYZER_OPTIONS --exclude ../src/tools/simulator/libsimulator_uv/linenoise.c"

  echo "#!/bin/bash
cd '$SCRIPT_DIR/$BUILD_DIR';
" >run-scan-build.sh
  if [[ -z "$CMAKE_CLANG_ANALYZER_PATH" ]]; then
    echo "env CCC_CC=\"$CCC_CC\" CCC_CXX=\"$CCC_CXX\" scan-build -o report --html-title='$PROJECT_NAME static analysis' $CMAKE_CLANG_ANALYZER_OPTIONS cmake --build . -j \$@" >>run-scan-build.sh
  else
    echo "env PATH=\"\$PATH:$CMAKE_CLANG_ANALYZER_PATH\" CCC_CC=\"$CCC_CC\" CCC_CXX=\"$CCC_CXX\" scan-build -o report --html-title='$PROJECT_NAME static analysis' $CMAKE_CLANG_ANALYZER_OPTIONS cmake --build . -j \$@" >>run-scan-build.sh
  fi
  chmod +x run-scan-build.sh
  echo "Now, you can run $PWD/run-scan-build.sh to build a static analysis report"
  echo "You can get help and binary of clang-analyzer and scan-build at http://clang-analyzer.llvm.org/scan-build.html"
fi
