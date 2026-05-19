#!/bin/bash

# 创建多个无规则文件，后续测试重命名脚本

set -euo pipefail

mkdir -p ../log

for _ in {1..20}; do
    filename="../log/$(uuidgen).txt"

    cat <<EOF >"$filename"
文件名: ${filename}
创建时间: $(date +"%Y-%m-%d %H:%M:%S")
随机数: ${RANDOM}
EOF

    echo "文件 ${filename} 创建成功"
done
