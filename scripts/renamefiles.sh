#!/bin/bash

# 重命名文件

set -euo pipefail

dir="../log/*.txt"
cnt=1
prefix="test"
# $dir   需要展开通配符
for file in $dir; do
    [ -f "$file" ] || continue
    newname="../log/${prefix}_${cnt}.txt"
    mv -n "$file" "$newname"
    echo "$file -> $newname"
    # cnt=$((cnt + 1))
    ((cnt++))
done
