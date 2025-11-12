#!/bin/bash
# set -x

# pintos/threads 디렉토리 이동
echo "🟢 [INFO] Move to pintos/threads directory"
cd ../pintos/threads || { echo "❌ [ERROR] 디렉토리 이동 실패"; exit 1; }

# 소스 정리 및 빌드
echo "⚙️  [BUILD] make clean && make"
make clean && make

if [ $? -eq 0 ]; then
    # build 디렉토리로 이동
    echo "🟢 [INFO] Move to build directory"
    cd build || { echo "❌ [ERROR] build 없음"; exit 1; }
    # 인자를 받아 테스트 선택 실행
    case "$1" in
        single|multiple|simultaneous|priority|zero|negative)
            echo "🧪 [TEST] Running alarm-$1 test"
            make tests/threads/alarm-$1.result
            ;;
        ""|all)
            echo "🧪 [TEST] Running all alarm tests"
            for t in single multiple simultaneous priority zero negative; do
                echo "🧪 [TEST] Running alarm-$t test"
                make tests/threads/alarm-$t.result
            done
            ;;
        *)
            echo "⚠️  [WARNING] Unknown argument: $1"
            echo "사용법: $0 [single|multiple|simultaneous|priority|zero|negative|all]"
            exit 1
            ;;
    esac
else
    echo "❌ [ERROR] Build Error! 빌드에 실패했습니다."
    exit 1
fi