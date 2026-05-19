#!/bin/bash

# set -e

# file="log.log"
# dir="log"

# if [ ! -d "${dir}" ]; then
#     echo "${dir} 不存在，创建目录"
#     mkdir -p "${dir}"
# fi

# cd "${dir}" || exit 1

# if [ ! -f "${file}" ]; then
#     touch "${file}"
#     echo "文件 ${file} 创建成功"
# else
#     echo "文件 ${file} 已存在"
# fi

# read -rp "输入数字(1-3): " num
# case ${num} in
# 1) echo "选择了1" ;;
# 2) echo "选择了2" ;;
# 3) echo "选择了3" ;;
# *) echo "无效输入" ;;
# esac

# for file in *.sh; do
#     echo "脚本文件 ${file} "
# done

# file=log.txt
# i=1
# while IFS= read -r line; do
#     echo "Line ${i}: ${line}"
#     i=$((i + 1))
# done <"${file}"

# add() {
#     # 位置参数变量
#     local a=$1 # 局部变量（仅函数内可见）
#     local b=$2
#     echo $((a + b)) # 通过 echo 返回结果（推荐，因 return 只能返回 0-255）
#     # return 0
# }

# sum=$(add 3 5)      # 调用函数，接收返回值
# echo "3 + 5 = $sum" # 输出：3 + 5 = 8

# tmp_file="/tmp/temp_data.txt"
# trap "rm -f $tmp_file; echo '临时文件已清理'" EXIT # EXIT 信号触发清理

# # 创建临时文件并写入数据
# echo "临时数据" >"$tmp_file"
# sleep 10 # 等待 10 秒，期间按 Ctrl+C 也会触发清理
