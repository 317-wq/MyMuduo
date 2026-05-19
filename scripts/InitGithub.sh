#!/bin/bash

set -e

read -p "项目名称: " PROJECT_NAME

if [ -d "$PROJECT_NAME" ]; then
    echo "目录已存在: $PROJECT_NAME"
    exit 1
fi

echo
echo "创建项目: $PROJECT_NAME"
echo

mkdir "$PROJECT_NAME"

cd "$PROJECT_NAME"

mkdir -p \
include \
src \
test \
build \
examples

touch \
README.md \
CMakeLists.txt \
.gitignore

cat > README.md << EOF
# $PROJECT_NAME

A lightweight C++ project.

## Build

\`\`\`bash
mkdir build
cd build
cmake ..
make -j
\`\`\`
EOF

cat > .gitignore << EOF
build/
cmake-build-*/

CMakeFiles/
CMakeCache.txt
Makefile
cmake_install.cmake
compile_commands.json

*.o
*.obj
*.so
*.a
*.out
*.exe

.vscode/
.idea/

*.log
*.swp
*~

.DS_Store
.cache/
EOF

git config --global init.defaultBranch main

git init
git branch -m main

git add .

git commit -m "初始化项目"

gh repo create "$PROJECT_NAME" \
--public \
--source=. \
--remote=origin \
--push

echo
echo "完成"
echo "本地:"
pwd

echo
echo "GitHub:"
echo "https://github.com/$(gh api user --jq .login)/$PROJECT_NAME"