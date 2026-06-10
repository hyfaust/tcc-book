#!/bin/bash
# build.sh - 构建 TinyCC 教科书静态网站
#
# 用法:
#   ./build.sh          # 构建网站
#   ./build.sh serve    # 启动本地预览服务器
#   ./build.sh clean    # 清理构建产物

set -e
cd "$(dirname "$0")"

# 激活虚拟环境
source .venv/bin/activate

case "${1:-build}" in
  build)
    echo "正在构建网站..."
    mkdocs build --clean
    echo ""
    echo "构建完成！输出目录: site/"
    echo "页面数: $(find site -name '*.html' | wc -l)"
    echo "总大小: $(du -sh site | cut -f1)"
    ;;
  serve)
    echo "启动本地预览服务器..."
    echo "访问 http://127.0.0.1:8000"
    mkdocs serve --dev-addr 0.0.0.0:8000
    ;;
  clean)
    echo "清理构建产物..."
    rm -rf site
    echo "已清理"
    ;;
  *)
    echo "用法: $0 [build|serve|clean]"
    exit 1
    ;;
esac
