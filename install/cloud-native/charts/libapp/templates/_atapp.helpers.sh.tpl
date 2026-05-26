{{- define "libapp.run.wrapper.prehook.sh" -}}
{{- $script_dir := (.script_dir | default "$SCRIPT_DIR") -}}
{{- $enable_sanitizer := (.enable_sanitizer | default false) -}}
{{- $enable_hook_malloc := (.enable_hook_malloc | default false) -}}
{{- $bus_addr := include "libapp.busAddr" . -}}
TRY_SANITIZER_NAME=""
if [[ -e "{{ $script_dir }}/package-version.txt" ]]; then
  TRY_SANITIZER_NAME="$(grep -i -F 'sanitizer type:' "{{ $script_dir }}/package-version.txt" || true)"
  TRY_SANITIZER_RUNTIME="$(grep -i -F 'sanitizer runtime:' "{{ $script_dir }}/package-version.txt" || true)"
  if [[ ! -z "$TRY_SANITIZER_NAME" ]]; then
    TRY_SANITIZER_NAME="$(echo "$TRY_SANITIZER_NAME" | awk 'BEGIN{FS=":"}{print $NF}' | xargs -r echo)"
  fi
  if [[ ! -z "$TRY_SANITIZER_RUNTIME" ]]; then
    TRY_SANITIZER_RUNTIME="$(echo "$TRY_SANITIZER_RUNTIME" | awk 'BEGIN{FS=":"}{print $NF}' | xargs -r echo)"
  fi
  TRY_SANITIZER_COMPILER_USE_GCC=1
  grep -i -F 'compiler:' "{{ $script_dir }}/package-version.txt" | grep -i gcc > /dev/null || TRY_SANITIZER_COMPILER_USE_GCC=0
fi

  {{- if $enable_sanitizer }}
  # Maybe remove ASAN_OPTIONS=sleep_before_dying=3:abort_on_error=1:halt_on_error=1
  # GCC 4.8.5 - Availale options can be see here https://github.com/gcc-mirror/gcc/blob/releases/gcc-4.8.5/libsanitizer/asan/asan_flags.h
  # export ASAN_OPTIONS=abort_on_error=1:disable_core=0:handle_segv=1:unmap_shadow_on_exit=1:atexit=1:log_path={{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.asan.log
  # GCC 7.3.1 - yum install -y tlinux-release-gcc-update; yum update -y;
  # GCC 7.3.1 - Availale options can be see here:
  #     https://github.com/gcc-mirror/gcc/blob/releases/gcc-7.3.0/libsanitizer/asan/asan_flags.h
  #     https://github.com/gcc-mirror/gcc/blob/releases/gcc-7.3.0/libsanitizer/sanitizer_common/sanitizer_flags.inc
  # export ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:detect_leaks=1:disable_coredump=0:handle_segv=1:handle_abort=0:handle_sigill=0:handle_sigfpe=1:unmap_shadow_on_exit=1:atexit=1:allow_addr2line=1:log_path={{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.asan.log
  {{- end }}
if [[ "x$TRY_SANITIZER_NAME" == "xaddress" ]]; then
  {{- if $enable_sanitizer }}
    export ASAN_OPTIONS="detect_odr_violation=1:halt_on_error=0:abort_on_error=0:detect_leaks=1:disable_coredump=0:handle_segv=1:handle_abort=0:handle_sigill=0:handle_sigfpe=1:unmap_shadow_on_exit=1:atexit=1:allow_addr2line=1:log_path={{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.asan.log"
    # echo "leak:FooBar" > "$PWD/lasn-suppr.txt" ;
    # export LSAN_OPTIONS=suppressions=$PWD/lasn-suppressions.txt
  {{- else }}
    export ASAN_OPTIONS="detect_odr_violation=0:report_globals=0:halt_on_error=0:abort_on_error=0:detect_leaks=0:disable_coredump=1:handle_segv=0:handle_abort=0:handle_sigill=0:handle_sigfpe=0:handle_sigbus=0"
  {{- end }}
  if [[ "x$TRY_SANITIZER_RUNTIME" != "xstatic" ]]; then
    if [[ $TRY_SANITIZER_COMPILER_USE_GCC -eq 0 ]]; then
      export LD_PRELOAD=$(find /opt/llvm-latest/ -name "libclang_rt.asan.so" | head -n 1)
    else
      export LD_PRELOAD=$(find /opt/gcc-latest/ -name "libasan.so" | head -n 1)
    fi
  fi
elif [[ "x$TRY_SANITIZER_NAME" == "xthread" ]]; then
  {{- if $enable_sanitizer }}
    # https://github.com/google/sanitizers/wiki/ThreadSanitizerFlags
    export TSAN_OPTIONS="halt_on_error=0:verbosity=0:stop_on_start=0:history_size=5:report_atomic_races=0:log_path={{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.tsan.log"
  {{- else }}
    export TSAN_OPTIONS="report_bugs=0:halt_on_error=0:abort_on_error=0:detect_leaks=0:disable_coredump=1:handle_segv=0:handle_abort=0:handle_sigill=0:handle_sigfpe=0:handle_sigbus=0"
  {{- end }}
  if [[ "x$TRY_SANITIZER_RUNTIME" != "xstatic" ]]; then
    if [[ $TRY_SANITIZER_COMPILER_USE_GCC -eq 0 ]]; then
      export LD_PRELOAD=$(find /opt/llvm-latest/ -name "libclang_rt.tsan.so" | head -n 1)
    else
      export LD_PRELOAD=$(find /opt/gcc-latest/ -name "libtsan.so" | head -n 1)
    fi
  fi
elif [[ "x$TRY_SANITIZER_NAME" == "xleak" ]]; then
  {{- if $enable_sanitizer }}
    # https://github.com/google/sanitizers/wiki/AddressSanitizerFlags
    export LSAN_OPTIONS="detect_odr_violation=1:halt_on_error=0:abort_on_error=0:detect_leaks=1:disable_coredump=0:handle_segv=1:handle_abort=0:handle_sigill=0:handle_sigfpe=1:unmap_shadow_on_exit=1:atexit=1:allow_addr2line=1:log_path={{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.lsan.log"
    # echo "leak:FooBar" > "$PWD/lasn-suppr.txt" ;
    # export LSAN_OPTIONS=suppressions=$PWD/lasn-suppressions.txt
  {{- else }}
    export LSAN_OPTIONS="detect_odr_violation=0:report_globals=0:halt_on_error=0:abort_on_error=0:detect_leaks=0:disable_coredump=1:handle_segv=0:handle_abort=0:handle_sigill=0:handle_sigfpe=0:handle_sigbus=0"
  {{- end }}
  if [[ "x$TRY_SANITIZER_RUNTIME" != "xstatic" ]]; then
    if [[ $TRY_SANITIZER_COMPILER_USE_GCC -eq 0 ]]; then
      export LD_PRELOAD=$(find /opt/llvm-latest/ -name "libclang_rt.lsan.so" | head -n 1)
    else
      export LD_PRELOAD=$(find /opt/gcc-latest/ -name "liblsan.so" | head -n 1)
    fi
  fi
elif [[ "x$TRY_SANITIZER_NAME" == "xundefined behavior" ]]; then
  {{- if $enable_sanitizer }}
    # https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
    export UBSAN_OPTIONS="print_stacktrace=1:log_path={{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.ubsan.log"
  {{- else }}
    export UBSAN_OPTIONS="halt_on_error=0:abort_on_error=0:detect_leaks=0:disable_coredump=1:handle_segv=0:handle_abort=0:handle_sigill=0:handle_sigfpe=0:handle_sigbus=0"
  {{- end }}
  if [[ "x$TRY_SANITIZER_RUNTIME" != "xstatic" ]]; then
    if [[ $TRY_SANITIZER_COMPILER_USE_GCC -eq 0 ]]; then
      export LD_PRELOAD=$((find /opt/llvm-latest/ -name "libclang_rt.ubsan_standalone.so" || /opt/llvm-latest/ -name "libclang_rt.ubsan_minimal.so") | head -n 1)
    else
      export LD_PRELOAD=$(find /opt/gcc-latest/ -name "libubsan.so" | head -n 1)
    fi
  fi
elif [[ "x$TRY_SANITIZER_NAME" == "xhardware address" ]]; then
  {{- if $enable_sanitizer }}
    # https://clang.llvm.org/docs/HardwareAssistedAddressSanitizerDesign.html
    export HWASAN_OPTIONS="detect_odr_violation=1:halt_on_error=0:abort_on_error=0:detect_leaks=1:disable_coredump=0:handle_segv=1:handle_abort=0:handle_sigill=0:handle_sigfpe=1:unmap_shadow_on_exit=1:atexit=1:allow_addr2line=1:log_path={{ .Values.server_log_dir }}/{{ include "libapp.name" . }}_{{ $bus_addr }}.hwasan.log"
  {{- else }}
    export HWASAN_OPTIONS="detect_odr_violation=0:report_globals=0:halt_on_error=0:abort_on_error=0:detect_leaks=0:disable_coredump=1:handle_segv=0:handle_abort=0:handle_sigill=0:handle_sigfpe=0:handle_sigbus=0"
  {{- end }}
  if [[ "x$TRY_SANITIZER_RUNTIME" != "xstatic" ]]; then
    if [[ $TRY_SANITIZER_COMPILER_USE_GCC -eq 0 ]]; then
      export LD_PRELOAD=$(find /opt/llvm-latest/ -name "libclang_rt.hwasan.so" | head -n 1)
    else
      export LD_PRELOAD=$(find /opt/gcc-latest/ -name "libhwasan.so" | head -n 1)
    fi
  fi
  {{- if $enable_hook_malloc }}
elif [[ -z "$TRY_SANITIZER_NAME" ]]; then
  FIND_MIMALLOC_SO_FILES=($(find "$PROJECT_INSTALL_DIR/lib" -name "libmimalloc.so*"));
  FIND_JEMALLOC_SO_FILES=($(find "$PROJECT_INSTALL_DIR/lib" -name "libjemalloc.so*"));
  if [[ ${#FIND_JEMALLOC_SO_FILES[@]} -gt 0 ]]; then
    FIND_JEMALLOC_SO=$(ls -d -t ${FIND_JEMALLOC_SO_FILES[@]} | head -n 1 | xargs readlink -f)
    export LD_PRELOAD="$FIND_JEMALLOC_SO"
    # Profile options(Maybe other useful options: prof_accum:true,prof_final:true,prof_leak:true,utrace:true,stats_print:true )
    # Use options prof_active:false and mallctl API
    # lg_prof_sample=0 means sample in every malloc,lg_prof_sample:19 means sample in every 512K
    # Using jeprof "{{ .Values.proc_name }}" --base=<from.heap> <target.heap> to view the diff.(--dot can be used when having graphviz)
    {{- if (dig "memory" "enable" false .Values.profiling ) }}
      {{- $profiling_memory_jemalloc_opts := (list) }}
      {{- if (dig "memory" "leak" false .Values.profiling ) }}
        {{- $profiling_memory_jemalloc_opts = append $profiling_memory_jemalloc_opts ",prof_leak:true" -}}
      {{- end }}
      {{- if (dig "memory" "cumulative_count" false .Values.profiling ) }}
        {{- $profiling_memory_jemalloc_opts = append $profiling_memory_jemalloc_opts ",prof_accum:true" -}}
      {{- end }}
      {{- if (dig "memory" "stats" false .Values.profiling ) }}
        {{- $profiling_memory_jemalloc_opts = append $profiling_memory_jemalloc_opts ",prof_final:true,stats_print:true" -}}
      {{- end }}
    export MALLOC_CONF="prof:true,lg_prof_sample:19,lg_prof_interval:28{{ join "" $profiling_memory_jemalloc_opts }},prof_prefix:../log/{{ include "libapp.name" . }}_{{ $bus_addr }}.jemalloc"
    {{- else }}
    # export MALLOC_CONF="prof:true,lg_prof_sample:19,lg_prof_interval:28,prof_prefix:../log/{{ include "libapp.name" . }}_{{ $bus_addr }}.jemalloc"
    {{- end }}
  elif [[ ${#FIND_MIMALLOC_SO_FILES[@]} -gt 0 ]]; then
    FIND_MIMALLOC_SO=$(ls -d -t ${FIND_MIMALLOC_SO_FILES[@]} | head -n 1 | xargs readlink -f)
    export LD_PRELOAD="$FIND_MIMALLOC_SO"
    # Debug and profile options
    {{- if (dig "memory" "enable" false .Values.profiling ) }}
      {{- if (dig "memory" "stats" false .Values.profiling ) }}
    export mimalloc_show_stats=1
      {{- else }}
    # export mimalloc_show_stats=1
      {{- end }}
    export mimalloc_verbose=1
    export mimalloc_show_errors=1
    {{- else }}
    # export mimalloc_show_stats=1
    # export mimalloc_verbose=1
    # export mimalloc_show_errors=1
    {{- end }}
  fi
  {{- end }}
fi
{{- end }}

{{- define "libapp.run.wrapper.posthook.sh" -}}
unset LD_PRELOAD
{{- end }}

{{- define "libapp.run.wrapper.sh" -}}
function atapp_run_wrapper() {
  {{ include "libapp.run.wrapper.prehook.sh" . | nindent 2 }}
  "$@"

  EXIT_CODE=$?

  {{ include "libapp.run.wrapper.posthook.sh" . | nindent 2 }}

  return $EXIT_CODE
}
{{- end }}
