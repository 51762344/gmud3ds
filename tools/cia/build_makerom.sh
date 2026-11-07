#!/usr/bin/env bash
# 拉取并编译 makerom（3DSGuy/Project_CTR 中的开源 makerom v0.19.0）。
#
# 用法（MSYS2 shell）：
#   bash tools/cia/build_makerom.sh
#   GIT_PROXY=http://127.0.0.1:7897 bash tools/cia/build_makerom.sh   # 需要代理时
#
# MSYS2 运行时不含 iconv，而 makerom 的 src/oschar.c 依赖 iconv 做 UTF 转换，
# 因此应用 tools/cia/makerom-oschar-iconv.patch，用自带的 UTF-8/UTF-16 转换替换。

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." ; pwd)"
cd "$ROOT"

TOOLS="$ROOT/.tools"
REPO="$TOOLS/Project_CTR"
URL="https://github.com/3DSGuy/Project_CTR.git"
PATCH="$ROOT/tools/cia/makerom-oschar-iconv.patch"

GIT_OPTS=()
if [ -n "${GIT_PROXY:-}" ]; then
  GIT_OPTS=(-c "http.proxy=$GIT_PROXY" -c "https.proxy=$GIT_PROXY")
fi

mkdir -p "$TOOLS"
if [ ! -d "$REPO/.git" ]; then
  echo "==> 克隆 Project_CTR（稀疏检出 makerom）"
  git "${GIT_OPTS[@]}" clone --depth 1 --filter=blob:none --sparse "$URL" "$REPO"
fi
git -C "$REPO" "${GIT_OPTS[@]}" sparse-checkout set makerom

if ! grep -q "os_ManualUTF8toUTF16" "$REPO/makerom/src/oschar.c" ; then
  echo "==> 应用 iconv 补丁"
  git -C "$REPO" apply "$PATCH"
fi

cd "$REPO/makerom"
echo "==> 编译依赖库（mbedtls / blz / yaml）"
make deps PROJECT_PLATFORM=GNU
echo "==> 编译 makerom"
make PROJECT_PLATFORM=GNU
ls -l bin/makerom.exe