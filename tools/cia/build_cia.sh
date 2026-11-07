#!/usr/bin/env bash
# 构建 GMUD 3DS 的 CIA 包。
#
# 用法（MSYS2 shell）：bash tools/cia/build_cia.sh
#
# 依赖：
#   - devkitPro 3ds-dev（make / 3dsxtool / smdhtool）
#   - .tools/Project_CTR/makerom/bin/makerom.exe（见 tools/cia/build_makerom.sh）
#
# 产物：
#   build/cia/gmud3ds-nocrypt.cia  不加密版，真机 CFW（FBI）与模拟器（Azahar）通用

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." ; pwd)"
cd "$ROOT"

MAKEROM="$ROOT/.tools/Project_CTR/makerom/bin/makerom.exe"
BANNERTOOL="${BANNERTOOL:-$ROOT/.tools/bannertool/pkg/windows-x86_64/bannertool.exe}"
BANNER_PNG="$ROOT/assets/banner/gmud_banner.png"
BANNER_WAV="$ROOT/assets/banner/gmud_banner.wav"
RSF="$ROOT/tools/cia/gmud3ds.rsf"
OUT="$ROOT/build/cia"
STAGE="$OUT/romfs_src"

# MSYS2 的 login shell 通常没有 python（PATH 里只有 devkitPro 工具链）。
# build_cia.ps1 会传 PYTHON；直接跑 bash 时按常见安装位置兜底。
if [ -z "${PYTHON:-}" ]; then
  if command -v python >/dev/null 2>&1; then
    PYTHON=python
  elif command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
  else
    for candidate in \
      /c/Users/*/anaconda3/python.exe \
      /c/Users/*/miniconda3/python.exe \
      /c/Users/*/AppData/Local/Programs/Python/Python*/python.exe \
      /c/ProgramData/anaconda3/python.exe \
      /c/ProgramData/miniconda3/python.exe; do
      if [ -x "$candidate" ]; then
        PYTHON="$candidate"
        break
      fi
    done
  fi
fi
if [ -z "${PYTHON:-}" ]; then
  echo "找不到 python；用 PYTHON=/path/to/python.exe bash tools/cia/build_cia.sh 指定。" >&2
  exit 1
fi

if [ ! -f "$MAKEROM" ]; then
  echo "找不到 makerom：$MAKEROM" >&2
  echo "先执行：bash tools/cia/build_makerom.sh" >&2
  exit 1
fi
if [ ! -f "$BANNERTOOL" ]; then
  echo "缺少 bannertool：$BANNERTOOL" >&2
  echo "设置 BANNERTOOL 环境变量指向 bannertool 可执行文件" >&2
  exit 1
fi
if [ ! -f "$BANNER_PNG" ] || [ ! -f "$BANNER_WAV" ]; then
  echo "缺少 BNR 素材：$BANNER_PNG / $BANNER_WAV" >&2
  echo "先执行：python tools/banner_build.py" >&2
  exit 1
fi

echo "==> 1/5 构建 3dsx / elf / smdh"
make

echo "==> 2/5 准备 romfs 暂存目录（排除 .stamp / .data.sha）"
rm -rf "$STAGE"
mkdir -p "$STAGE"
for f in romfs/* ; do
  cp -r "$f" "$STAGE/"
done

mkdir -p "$OUT"

echo "==> 3/5 生成 BNR 横幅（CBMD + CWAV）"
# 普通横幅：256x128 PNG + WAV，HOME 菜单按平面图直接显示，不做 3D 摆动。
# SMDH 的 extended banner 位（0x2024 bit5）必须为 0，否则 HOME 会去 CBMD 里
# 找 CGFX 场景而不是这张图。smdhtool 没有 flag 开关，且已有的 SMDH 可能带
# 旧构建留下的标志，所以打包前显式清一次。
"$BANNERTOOL" makebanner -i "$BANNER_PNG" -a "$BANNER_WAV" \
  -o "$OUT/gmud_banner.bnr"
"$PYTHON" "$ROOT/tools/smdh_flag.py" --clear gmud3ds.smdh

echo "==> 4/5 生成 CIA（不加密版）"
# 只出不加密版：加密版的内容用开发密钥集加密，真机 AM 用零售 common key 解不出
# title key，FBI 报 0xD8E0806A (Cert signature or hash check failed)，装不上；
# 不加密版真机（Luma + FBI）与模拟器（Azahar）都能装。
sed 's/EnableCrypt             : true/EnableCrypt             : false/' "$RSF" > "$OUT/gmud3ds-nocrypt.rsf"
"$MAKEROM" -f cia -o "$OUT/gmud3ds-nocrypt.cia" \
  -rsf "$OUT/gmud3ds-nocrypt.rsf" -target d \
  -elf gmud3ds.elf -icon gmud3ds.smdh -banner "$OUT/gmud_banner.bnr" \
  -desc app:4 -exefslogo
rm -f "$OUT/gmud3ds.cia"   # 清掉旧构建，避免误装加密版

echo "==> 5/5 完成"
ls -l "$OUT"/*.cia "$OUT"/*.bnr
sha256sum "$OUT/gmud3ds-nocrypt.cia"