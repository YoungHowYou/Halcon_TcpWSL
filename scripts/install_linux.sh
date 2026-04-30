#!/usr/bin/env bash
# Halcon_TcpWSL — Linux 一键部署脚本
#
# 用法（在工程根目录或 scripts/ 目录里都行）:
#   sudo -E env "HALCONROOT=$HALCONROOT" bash scripts/install_linux.sh
#   # 或者先 export HALCONROOT，再 bash scripts/install_linux.sh（脚本内部按需 sudo）
#
# 脚本会做：
#   1. 校验 HALCON 安装
#   2. apt 装运行时依赖（oneVPL GPU runtime、Intel VAAPI 驱动等）
#   3. 把当前用户加入 render 组（持久）+ setfacl 立即生效（重启失效）
#   4. 生成/修复 /etc/environment（带备份），路径全部用绝对路径
#   5. cmake 配置 + 编译，把产物同步到 lib/x64-linux/
#   6. ldd 检查 + H264 编码器 smoke test
#
# 全程幂等，重复跑不会破坏现有配置。

set -euo pipefail

# -------------------------------------------------------------------- 颜色
red()    { printf '\033[31m%s\033[0m\n' "$*"; }
green()  { printf '\033[32m%s\033[0m\n' "$*"; }
yellow() { printf '\033[33m%s\033[0m\n' "$*"; }
blue()   { printf '\033[34m%s\033[0m\n' "$*"; }
step()   { printf '\n\033[1;36m▸ %s\033[0m\n' "$*"; }

# -------------------------------------------------------------------- 路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

blue "=========================================="
blue "  Halcon_TcpWSL Linux 一键部署"
blue "=========================================="
echo "工程根: $PROJECT_ROOT"

# 提前刷新 sudo 凭证缓存，避免后面每个 sudo 都重新问密码
yellow "→ 需要 sudo 权限，输入一次密码后会缓存..."
sudo -v || { red "sudo 验证失败"; exit 1; }
# 后台保活 sudo 缓存，直到脚本结束
( while true; do sudo -n true 2>/dev/null; sleep 60; kill -0 "$$" 2>/dev/null || exit; done ) &
SUDO_KEEPALIVE_PID=$!
trap "kill $SUDO_KEEPALIVE_PID 2>/dev/null || true" EXIT

# -------------------------------------------------------------------- 1. HALCON
step "1/6 校验 HALCON 安装"

# 优先用 sudo 调用时被保留的环境变量；其次尝试从 /etc/environment 解析
if [ -z "${HALCONROOT:-}" ] && [ -f /etc/environment ]; then
    HALCONROOT="$(awk -F= '/^HALCONROOT=/ {gsub(/"/,"",$2); print $2; exit}' /etc/environment || true)"
fi
if [ -z "${HALCONROOT:-}" ]; then
    red "HALCONROOT 未设置"
    echo "请在 shell 里 export HALCONROOT=/path/to/HALCON-24.11-Progress-Steady"
    echo "或者编辑 /etc/environment 加上：HALCONROOT=\"...\""
    exit 1
fi
HALCONROOT="$(realpath "$HALCONROOT")"
HALCON_LIB="$HALCONROOT/lib/x64-linux"
if [ ! -f "$HALCON_LIB/libhalconcpp.so" ]; then
    red "$HALCONROOT 不像 HALCON 安装目录（找不到 lib/x64-linux/libhalconcpp.so）"
    exit 1
fi
green "✓ HALCON: $HALCONROOT"

# -------------------------------------------------------------------- 2. apt
step "2/6 安装系统依赖（apt，需要 sudo）"

PKGS=(
    build-essential cmake
    libmfx-gen1.2 intel-media-va-driver-non-free libva-drm2
)
MISSING=()
for p in "${PKGS[@]}"; do
    dpkg -s "$p" >/dev/null 2>&1 || MISSING+=("$p")
done
if [ ${#MISSING[@]} -eq 0 ]; then
    green "✓ 全部已装：${PKGS[*]}"
else
    yellow "需要安装：${MISSING[*]}"
    sudo apt-get update
    sudo apt-get install -y "${MISSING[@]}"
fi

# -------------------------------------------------------------------- 3. render group + ACL
step "3/6 GPU 设备权限（render 组 + ACL）"

USERNAME="${SUDO_USER:-$USER}"
if id -nG "$USERNAME" 2>/dev/null | grep -qw render; then
    green "✓ $USERNAME 已在 render 组"
else
    yellow "→ 把 $USERNAME 加入 render 组（持久，需重登才生效）"
    sudo usermod -aG render "$USERNAME"
fi

# setfacl 让本次会话立即可用（重启失效，但 render 组在重登后接管）
for dev in /dev/dri/renderD128 /dev/dri/card0; do
    if [ -e "$dev" ]; then
        if getfacl "$dev" 2>/dev/null | grep -q "user:$USERNAME:rw"; then
            green "✓ ACL: $dev 已对 $USERNAME 开放"
        else
            yellow "→ setfacl: $dev"
            sudo setfacl -m "u:$USERNAME:rw" "$dev" || true
        fi
    fi
done

# -------------------------------------------------------------------- 4. /etc/environment
step "4/6 配置 /etc/environment"

ENV_FILE=/etc/environment
if [ ! -f "$ENV_FILE" ]; then
    sudo touch "$ENV_FILE"
fi
ENV_BACKUP="${ENV_FILE}.bak.$(date +%Y%m%d_%H%M%S)"
sudo cp -a "$ENV_FILE" "$ENV_BACKUP"
green "✓ 备份: $ENV_BACKUP"

# 用 python 安全地编辑 /etc/environment：
#   - 修复反斜杠续行 + 缩进引入的脏字符
#   - 设置/合并 HALCON 系列变量
#   - LD_LIBRARY_PATH / HALCONEXTENSIONS 用列表方式合并，去重
sudo python3 - "$HALCONROOT" "$HALCON_LIB" "$PROJECT_ROOT" "$ENV_FILE" <<'PY'
import re, sys, os

halcon_root, halcon_lib, project_root, path = sys.argv[1:5]
project_lib = os.path.join(project_root, "lib", "x64-linux")
src = open(path).read()

# 1) 修复反斜杠续行
src = re.sub(r'\\\n[ \t]*', '', src)

# 2) helpers
def get_var(text, key):
    m = re.search(rf'^{key}\s*=\s*"([^"]*)"', text, re.M)
    return m.group(1) if m else None

def set_var(text, key, value):
    line = f'{key}="{value}"'
    pat = rf'^{key}\s*=.*$'
    if re.search(pat, text, re.M):
        return re.sub(pat, line, text, flags=re.M)
    if not text.endswith('\n'):
        text += '\n'
    return text + line + '\n'

# 3) 标量项
src = set_var(src, "HALCONROOT", halcon_root)
src = set_var(src, "HALCONARCH", "x64-linux")
src = set_var(src, "HALCONEXAMPLES", os.path.join(halcon_root, "examples"))

# 4) 列表项（合并去重，保持原有顺序）
def merge_list(text, key, must_have):
    cur = (get_var(text, key) or "").split(":")
    cur = [p.strip() for p in cur if p.strip()]
    for p in must_have:
        if p not in cur:
            cur.append(p)
    return set_var(text, key, ":".join(cur))

src = merge_list(src, "HALCONEXTENSIONS", [project_root])
src = merge_list(src, "LD_LIBRARY_PATH", [halcon_lib, project_lib])

open(path, "w").write(src)
print(f"  → 已写入 {path}")
PY

# -------------------------------------------------------------------- 5. 编译
step "5/6 编译 Halcon_TcpWSL"

export HALCONROOT
export HALCONEXAMPLES="$HALCONROOT/examples"
rm -rf build_linux
cmake -S . -B build_linux >/tmp/cmake_config.log 2>&1 || {
    red "CMake 配置失败，日志: /tmp/cmake_config.log"
    tail -30 /tmp/cmake_config.log
    exit 1
}
cmake --build build_linux -j >/tmp/cmake_build.log 2>&1 || {
    red "编译失败，日志: /tmp/cmake_build.log"
    tail -30 /tmp/cmake_build.log
    exit 1
}
green "✓ 编译完成"

# 同步到 lib/x64-linux/（HALCONEXTENSIONS 标准位置）
mkdir -p lib/x64-linux
cp -f bin/libHalcon_TcpWSL.so   lib/x64-linux/
cp -f bin/libHalcon_TcpWSLc.so  lib/x64-linux/
cp -f bin/libHalcon_TcpWSLcpp.so lib/x64-linux/
green "✓ 已同步 lib/x64-linux/"

# -------------------------------------------------------------------- 6. Smoke test
step "6/6 Smoke test"

# 6.1 ldd 没 not found
NF="$(LD_LIBRARY_PATH="$HALCON_LIB:${LD_LIBRARY_PATH:-}" ldd lib/x64-linux/libHalcon_TcpWSL.so | grep 'not found' || true)"
if [ -n "$NF" ]; then
    red "✗ 依赖未解析:"
    echo "$NF"
    exit 1
fi
green "✓ ldd 全部解析"

# 6.2 H264 init 测试
yellow "→ H264 编码器初始化测试..."
g++ -std=c++17 \
    "$SCRIPT_DIR/test_h264_init.cpp" \
    -I "$PROJECT_ROOT/3rd/DjiH264Encoder/include" \
    -L "$PROJECT_ROOT/3rd/DjiH264Encoder/lib/x64-linux" -lvpl \
    -Wl,-rpath,"$PROJECT_ROOT/3rd/DjiH264Encoder/lib/x64-linux" \
    -Wno-deprecated-declarations \
    -o /tmp/halcon_tcpwsl_h264_smoke 2>/tmp/smoke_compile.log || {
    red "smoke test 编译失败，日志: /tmp/smoke_compile.log"
    exit 1
}

if /tmp/halcon_tcpwsl_h264_smoke; then
    green "✓ H264 编码器可用"
else
    yellow "⚠ H264 编码器初始化失败（上面的报错给了原因）"
    yellow "  最常见原因：当前 shell 还没拿到 render 组权限"
    yellow "  解决：GUI 完整注销重登 一次，或重启"
    yellow "  本脚本已 setfacl 临时给了权限，重启会丢，但 render 组重登后会接管"
fi

# -------------------------------------------------------------------- 完成
echo
green "=========================================="
green "  部署完成"
green "=========================================="
echo "下一步:"
echo "  1. 如果 H264 smoke test 失败，GUI 完整注销重登一次（让 render 组生效）"
echo "  2. 启动 HDevelop，会自动从 HALCONEXTENSIONS 找到本扩展包"
echo "     export 的当前 shell 也可以直接 hdevelop & 启动"
echo "  3. 备份: $ENV_BACKUP（如出问题可还原）"
