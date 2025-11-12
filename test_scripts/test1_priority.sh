#!/bin/bash
# set -x

# pintos/userprog 디렉토리 이동
echo "🟢 [INFO] Move to pintos/threads directory"
cd ../pintos/threads || { echo "❌ [ERROR] 디렉토리 이동 실패"; exit 1; }

# 소스 정리 및 빌드
echo "⚙️  [BUILD] make clean && make"
make clean && make

if [ $? -eq 0 ]; then
    # build 디렉토리로 이동
    echo "🟢 [INFO] Move to build directory"
    cd build || { echo "❌ [ERROR] build 디렉토리 없음"; exit 1; }
    
    # 인자를 받아 테스트 선택 실행
    case "$1" in
        change|donate-one|donate-multiple|donate-multiple2|donate-nest|donate-sema|donate-lower|fifo|preempt|sema|condvar|donate-chain)
            echo "🧪 [TEST] Running priority-$1 test"
            make tests/threads/priority-$1.result
            ;;
        ""|all)
            echo "🧪 [TEST] Running all priority tests"
            for t in change donate-one donate-multiple donate-multiple2 donate-nest donate-sema donate-lower fifo preempt sema condvar donate-chain; do
                echo "🧪 [TEST] Running priority-$t test"
                make tests/threads/priority-$t.result
            done
            ;;
        *)
            echo "⚠️  [WARNING] Unknown argument: $1"
            echo "사용법: $0 [change|donate-one|donate-multiple|donate-multiple2|donate-nest|donate-sema|donate-lower|fifo|preempt|sema|condvar|donate-chain|all]"
            exit 1
            ;;
    esac
else
    echo "❌ [ERROR] Build Error! 빌드에 실패했습니다."
    exit 1
fi