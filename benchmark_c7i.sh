#!/usr/bin/env bash
set -euo pipefail

# ──────────────────────────────────────────────────────────────────────
# benchmark_c7i.sh — Install Clang 16, build LLTI, run benchmarks
# Target: AWS c7i (Intel Sapphire Rapids), Ubuntu 22.04/24.04
#
# Usage:
#   ./benchmark_c7i.sh                                     # Standard benchmarks
#   ./benchmark_c7i.sh --filter='BM_EytzingerLookup_10M'  # Single benchmark
#   ./benchmark_c7i.sh --topdown                           # TMA L1 analysis
#   ./benchmark_c7i.sh --toplev                            # TMA L2 analysis
#   ./benchmark_c7i.sh --toplev --toplev-level=3           # TMA L3
#   ./benchmark_c7i.sh --setup-isolation                   # Core isolation for c7i.large
#   ./benchmark_c7i.sh --setup-isolation=c7i.xlarge        # Core isolation for c7i.xlarge
#   ./benchmark_c7i.sh --fix-perf                          # Fix broken perf on newer AWS kernels
#   ./benchmark_c7i.sh --repetitions=10                    # Custom repetition count
# ──────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOPDOWN_MODE=false
TOPLEV_MODE=false
TOPLEV_LEVEL=2
SETUP_ISOLATION=false
INSTANCE_SIZE=""
FIX_PERF=false
BENCHMARK_FILTER=""
REPETITIONS=10

for arg in "$@"; do
    case "$arg" in
        --topdown) TOPDOWN_MODE=true ;;
        --toplev) TOPLEV_MODE=true ;;
        --toplev-level=*) TOPLEV_LEVEL="${arg#*=}" ;;
        --setup-isolation=*) SETUP_ISOLATION=true; INSTANCE_SIZE="${arg#*=}" ;;
        --setup-isolation) SETUP_ISOLATION=true ;;
        --fix-perf) FIX_PERF=true ;;
        --filter=*) BENCHMARK_FILTER="${arg#*=}" ;;
        --repetitions=*) REPETITIONS="${arg#*=}" ;;
    esac
done

# ── Helper: fix perf installation ──────────────────────────────────
fix_perf() {
    echo "==> Installing linux-tools for perf..."
    sudo apt-get update -qq
    
    # Split installs so failure of a specific version doesn't block the meta-package
    # Use noninteractive to bypass 'needrestart' or kernel upgrade prompts
    export DEBIAN_FRONTEND=noninteractive
    sudo -E apt-get install -y -qq linux-tools-aws linux-tools-generic linux-cloud-tools-aws 2>/dev/null || true
    sudo -E apt-get install -y -qq "linux-tools-$(uname -r)" 2>/dev/null || true

    sudo sysctl -w kernel.perf_event_paranoid=1 kernel.kptr_restrict=0 >/dev/null

    if perf stat -- true &>/dev/null; then
        echo "    perf works: $(perf --version)"
        return 0
    fi

    echo "    perf wrapper not functional, searching /usr/lib for compatible binaries..."
    WORKING_PERF=""
    for candidate in $(find /usr/lib -name perf -type f 2>/dev/null | grep tools); do
        if "$candidate" stat -- true &>/dev/null; then
            WORKING_PERF="$candidate"
            echo "    Found working binary: $candidate"
            break
        else
            echo "    Skipping incompatible: $candidate"
        fi
    done

    if [ -z "$WORKING_PERF" ]; then
        echo "ERROR: No compatible perf binary found."
        return 1
    fi

    sudo ln -sf "$WORKING_PERF" /usr/local/bin/perf
    hash -r
    if perf stat -- true &>/dev/null; then
        echo "    perf verified: $(perf --version)"
        return 0
    else
        echo "ERROR: perf still not functional after all fix attempts."
        return 1
    fi
}

# ── 0a. Standalone --fix-perf ─────────────────────────────────────
if $FIX_PERF; then
    if perf stat -- true &>/dev/null; then
        echo "==> perf already works: $(perf --version)"
    else
        fix_perf
    fi
    if ! $TOPDOWN_MODE && ! $TOPLEV_MODE && ! $SETUP_ISOLATION; then
        exit 0
    fi
fi

# ── 0b. Core isolation setup (if --setup-isolation) ───────────────
if $SETUP_ISOLATION; then
    case "$INSTANCE_SIZE" in
        c7i.large|"")
            ISOLATED="1"
            ;;
        c7i.xlarge)
            ISOLATED="1,3"
            ;;
        *)
            echo "ERROR: Unknown instance size '$INSTANCE_SIZE'. Use c7i.large or c7i.xlarge."
            exit 1
            ;;
    esac
    ISOLATION_PARAMS="isolcpus=${ISOLATED} nohz_full=${ISOLATED} rcu_nocbs=${ISOLATED}"
    echo "==> Configuring core isolation for ${INSTANCE_SIZE:-c7i.large} (${ISOLATION_PARAMS})..."

    CLOUDIMG_GRUB="/etc/default/grub.d/50-cloudimg-settings.cfg"
    if [ -f "$CLOUDIMG_GRUB" ]; then
        GRUB_FILE="$CLOUDIMG_GRUB"
        echo "    EC2 detected: using ${GRUB_FILE}"
    else
        GRUB_FILE="/etc/default/grub"
    fi

    if grep -q "isolcpus=" "$GRUB_FILE"; then
        echo "    Core isolation already configured in ${GRUB_FILE}:"
        grep "GRUB_CMDLINE_LINUX_DEFAULT" "$GRUB_FILE"
        echo "    No changes made. Reboot if not yet applied."
        exit 0
    fi

    sudo sed -i "s/^GRUB_CMDLINE_LINUX_DEFAULT=\"\(.*\)\"/GRUB_CMDLINE_LINUX_DEFAULT=\"\1 ${ISOLATION_PARAMS}\"/" "$GRUB_FILE"
    echo "    Updated ${GRUB_FILE}:"
    grep "GRUB_CMDLINE_LINUX_DEFAULT" "$GRUB_FILE"

    sudo update-grub
    echo ""
    echo "==> Core isolation configured. REBOOT REQUIRED to take effect."
    echo "    After reboot, verify with:"
    echo "      cat /proc/cmdline | grep isolcpus"
    echo "      cat /sys/devices/system/cpu/isolated"
    echo ""
    read -rp "Reboot now? [y/N] " answer
    if [[ "$answer" =~ ^[Yy]$ ]]; then
        sudo reboot
    fi
    exit 0
fi

# ── 1. Install Clang 16 ─────────────────────────────────────────────
echo "==> Installing Clang 16..."
if ! command -v clang++-16 &>/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    sudo rm -f /etc/apt/sources.list.d/llvm-16.list
    sudo apt-get update -qq
    sudo -E apt-get install -y -qq lsb-release wget software-properties-common gnupg cmake git build-essential python3-pip

    if sudo -E apt-get install -y -qq clang-16 lld-16 2>/dev/null; then
        echo "    Installed clang-16 from default repos."
    else
        wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/llvm-archive-keyring.gpg 2>/dev/null || true
        CODENAME=$(lsb_release -cs)
        echo "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] http://apt.llvm.org/${CODENAME}/ llvm-toolchain-${CODENAME}-16 main" \
            | sudo tee /etc/apt/sources.list.d/llvm-16.list >/dev/null
        sudo apt-get update -qq
        sudo -E apt-get install -y -qq clang-16 lld-16
    fi
else
    echo "    clang++-16 already installed: $(clang++-16 --version | head -1)"
fi

# ── 1b. Install perf (if profiling modes) ───────────────────────────
if $TOPDOWN_MODE || $TOPLEV_MODE; then
    if ! perf stat -- true &>/dev/null; then
        fix_perf || exit 1
    else
        echo "==> perf already installed: $(perf --version)"
    fi
    echo "==> Relaxing performance counter restrictions..."
    sudo sysctl -w kernel.perf_event_paranoid=1 kernel.kptr_restrict=0 >/dev/null
fi

# ── 2. Build ─────────────────────────────────────────────────────────
echo "==> Building (Clang 16, Release, -march=native)..."
rm -rf "${SCRIPT_DIR}/build"
mkdir "${SCRIPT_DIR}/build"
cmake -S "${SCRIPT_DIR}" -B "${SCRIPT_DIR}/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang-16 \
    -DCMAKE_CXX_COMPILER=clang++-16 \
    -DCMAKE_CXX_FLAGS="-march=native" \
    2>&1 | tail -3
cmake --build "${SCRIPT_DIR}/build" -j"$(nproc)" 2>&1 | tail -20

# ── 3. Run tests ─────────────────────────────────────────────────────
echo "==> Running tests..."
"${SCRIPT_DIR}/build/llti_tests"

# ── 4. Check core isolation status ─────────────────────────────────
ISOLATED_CORES=""
if [ -f /sys/devices/system/cpu/isolated ]; then
    ISOLATED_CORES=$(cat /sys/devices/system/cpu/isolated)
fi
if [ -n "$ISOLATED_CORES" ]; then
    echo "==> Core isolation active: CPUs ${ISOLATED_CORES} isolated"
else
    echo "==> WARNING: No core isolation detected. Results may be noisy."
    echo "    Run: ./benchmark_c7i.sh --setup-isolation=c7i.xlarge"
fi

# ── 5. Print header ─────────────────────────────────────────────────
echo ""
echo "================================================================"
echo "  LLTI Benchmark — Sapphire Rapids (AWS c7i) — $(date +%Y-%m-%d)"
echo "================================================================"
echo "  CPU:      $(lscpu | grep 'Model name' | sed 's/.*:\s*//')"
echo "  Compiler: $(clang++-16 --version | head -1)"
echo "  OS:       $(lsb_release -ds 2>/dev/null || grep PRETTY_NAME /etc/os-release | cut -d= -f2 | tr -d '\"')"
echo "  Instance: ${INSTANCE_TYPE:-c7i (set INSTANCE_TYPE env var)}"
echo "================================================================"
echo ""

# ── 6. Run benchmarks ───────────────────────────────────────────────
if ! $TOPDOWN_MODE && ! $TOPLEV_MODE; then
    BENCHMARK_OUT="${SCRIPT_DIR}/benchmark_results_$(date +%Y%m%d_%H%M%S).txt"
    BENCH_CMD=(taskset -c 1 "${SCRIPT_DIR}/build/llti_benchmarks"
        --benchmark_repetitions="${REPETITIONS}")
    if [ -n "$BENCHMARK_FILTER" ]; then
        BENCH_CMD+=(--benchmark_filter="${BENCHMARK_FILTER}")
    fi
    "${BENCH_CMD[@]}" 2>&1 | tee "${BENCHMARK_OUT}"
    echo ""
    echo "==> Results saved to ${BENCHMARK_OUT}"
fi

# ── 7. TMA: toplev.py (if --topdown or --toplev) ────────────────────
if $TOPDOWN_MODE || $TOPLEV_MODE; then
    echo ""
    echo "==> Setting up pmu-tools for toplev.py..."
    PMU_TOOLS_DIR="${SCRIPT_DIR}/third_party/pmu-tools"
    if [ ! -d "${PMU_TOOLS_DIR}" ]; then
        echo "    Cloning pmu-tools..."
        mkdir -p "${SCRIPT_DIR}/third_party"
        git clone --depth 1 https://github.com/andikleen/pmu-tools.git "${PMU_TOOLS_DIR}"
    else
        echo "    pmu-tools already present."
    fi

    if $TOPDOWN_MODE && ! $TOPLEV_MODE; then
        TMA_LEVEL=1
    else
        TMA_LEVEL="${TOPLEV_LEVEL}"
    fi

    TOPLEV_FILTER="${BENCHMARK_FILTER:-BM_EytzingerLookup_10M}"

    echo "==> Running toplev.py -l${TMA_LEVEL} (filter: ${TOPLEV_FILTER})..."
    TOPLEV_OUT="${SCRIPT_DIR}/toplev_results_$(date +%Y%m%d_%H%M%S).txt"
    sudo python3 "${PMU_TOOLS_DIR}/toplev.py" \
        --force-cpu spr --core S0-C0 -l"${TMA_LEVEL}" -v --no-desc \
        taskset -c 1 "${SCRIPT_DIR}/build/llti_benchmarks" \
        --benchmark_filter="${TOPLEV_FILTER}" \
        --benchmark_repetitions=3 \
        2>&1 | tee "${TOPLEV_OUT}"
    echo ""
    echo "==> Toplev results saved to ${TOPLEV_OUT}"
fi

echo ""
echo "==> Done."
