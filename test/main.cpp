// [FIX] 时间轮修正与测试
// 修改位置已在代码中用 // [FIX] 注释标明

#include <cstdint>
#include <functional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <iostream>
#include <cassert>
#include <algorithm>

// 定时器任务对象
class TimerTask {
public:
    using u64 = uint64_t;
    using u32 = uint32_t;
    using TaskObj = std::function<void()>;
    using RemoveObj = std::function<void()>;

    // [FIX 1] 构造函数增加 id 参数，并初始化 _id
    TimerTask(u64 id, u32 delay_time, TaskObj task_cb)
        : _id(id)
        , _delay_time(delay_time)
        , _task_cb(std::move(task_cb))
    {}

    u32 GetDelayTime() const { return _delay_time; }
    u64 GetId() const { return _id; }

    void SetRemoveCb(RemoveObj rm_task_cb) {
        _rm_task_cb = std::move(rm_task_cb);
    }

    // [FIX 2] 取消任务：清空回调，析构时不再执行
    void Cancel() {
        _canceled = true;
        _task_cb = nullptr;
        _rm_task_cb = nullptr;
    }

    // [FIX 2] 析构时执行回调（如果未被取消），并清理 _task_map
    ~TimerTask() {
        // 两层删除逻辑
        // 一个就是主动删除
        // 一个是tick向后走 clear完之后去析构
        if (!_canceled && _task_cb)
            _task_cb();
        if (_rm_task_cb)
            _rm_task_cb();
    }

private:
    bool _canceled = false;      // [FIX 2] 取消标记
    u64 _id;                    // [FIX 1] 现在由构造函数正确初始化
    u32 _delay_time;
    TaskObj _task_cb;
    RemoveObj _rm_task_cb;      // [FIX 2] 析构时调用此回调清理 map
};

// 时间轮 -> 管理所有的定时器任务对象
class TimerWheel {
public:
    using u64 = uint64_t;
    using u32 = uint32_t;
    using SharedTaskPtr = std::shared_ptr<TimerTask>;
    using WeakTaskPtr = std::weak_ptr<TimerTask>;

    static constexpr int CAPACITY = 60;

    TimerWheel(int capacity = CAPACITY)
        : _tick(0)
        , _capacity(capacity)
        , _wheel(_capacity)
    {}

    // [FIX 3] 移除变量遮蔽 (shadowing)，
    //         不再用 pt->GetId()/GetDelayTime() 覆盖参数
    // [FIX 8] 添加重复 ID 处理：先取消旧任务再添加新任务
    void AddTask(u64 id, u32 delay, const TimerTask::TaskObj& cb) {
        // 如果 ID 已存在，先取消旧任务（从 wheel 移除并清空回调）
        if (_task_map.count(id))
            removeTaskImpl(id);

        SharedTaskPtr pt = std::make_shared<TimerTask>(id, delay, cb);
        pt->SetRemoveCb(std::bind(&TimerWheel::RemoveTask, this, id));

        int pos = (_tick + delay) % _capacity;
        _task_map[id] = pt;
        _wheel[pos].push_back(pt);
    }

    // [FIX 4] 修正函数名拼写: RefershTask -> RefreshTask
    // [FIX 5] 使用 find() 替代 operator[] + count()，同时清理已过期的 weak_ptr
    void RefreshTask(u64 id) {
        auto it = _task_map.find(id);
        if (it == _task_map.end())
            return;

        SharedTaskPtr pt = it->second.lock();
        if (pt == nullptr) {
            _task_map.erase(it);     // [FIX 5] 清理已过期的条目
            return;
        }

        u32 delay = pt->GetDelayTime();
        int pos = (_tick + delay) % _capacity;
        _wheel[pos].push_back(pt);
    }

    // 推动时间轮走一格，执行到期任务
    void RunTick() {
        _tick = (_tick + 1) % _capacity;
        _wheel[_tick].clear();  // 析构 shared_ptr，触发 TimerTask::~TimerTask
    }

    // [FIX 6] 暴露内部状态，方便测试验证
    int GetTick() const { return _tick; }
    size_t MapSize() const { return _task_map.size(); }

    // [FIX 6] 暴露 RemoveTask 供测试主动删除
    void RemoveTask(u64 id) { removeTaskImpl(id); }

    void Debug(){
        std::cout << "打印map：";
        for(auto &kv : _task_map){
            auto sp = kv.second.lock();
            if (sp)
                std::cout << sp->GetId() << " ";
            else
                std::cout << "(expired) ";
        }
    }
private:
    // [FIX 7] RemoveTask 实现：
    //   - erase map 后尝试 lock weak_ptr
    //   - lock 成功 → 外部主动删除，从 wheel 中移除
    //   - lock 失败 → 由析构函数触发，wheel 已在清理中，无需额外操作
    void removeTaskImpl(u64 id) {
        auto it = _task_map.find(id);
        if (it == _task_map.end())
            return;

        auto wk = it->second;
        _task_map.erase(it);

        // 主动删除任务
        if (auto sp = wk.lock()) {
            // [FIX 2] 先取消再移除，防止析构时执行回调
            sp->Cancel();

            u32 delay = sp->GetDelayTime();
            int pos = (_tick + delay) % _capacity;

            auto& slot = _wheel[pos];
            slot.erase(
                std::remove_if(slot.begin(), slot.end(),
                               [id](const SharedTaskPtr& p) {
                                   return p->GetId() == id;
                               }),
                slot.end());
        }
    }

    int _tick;
    int _capacity;
    std::vector<std::vector<SharedTaskPtr>> _wheel;
    std::unordered_map<u64, WeakTaskPtr> _task_map;
};

// ===================== 测试 =====================

int main() {
    std::cout << "=== 时间轮测试 ===" << std::endl;

    // 测试 1: 基本到期执行
    {
        std::cout << "测试1: 基本到期执行 ... \n";
        TimerWheel tw;
        int counter = 0;
        tw.AddTask(1, 3, [&counter] { ++counter; });
        tw.AddTask(2, 3, [&counter] { ++counter; });
        tw.AddTask(3, 3, [&counter] { ++counter; });
        tw.AddTask(4, 3, [&counter] { ++counter; });
        tw.AddTask(5, 3, [&counter] { ++counter; });
        tw.Debug();
        for (int i = 0; i < 3; ++i){
            tw.RunTick();
            tw.Debug();
        }
        assert(counter == 5);
        assert(tw.MapSize() == 0);  // map 应该已清理
        std::cout << "\n通过" << std::endl;
    }

    // // 测试 2: 主动删除任务
    // {
    //     std::cout << "测试2: 主动删除任务（不执行回调） ... ";
    //     TimerWheel tw;
    //     int counter = 0;
    //     tw.AddTask(1, 5, [&counter] { ++counter; });

    //     tw.RemoveTask(1);
    //     assert(tw.MapSize() == 0);

    //     for (int i = 0; i < 6; ++i)
    //         tw.RunTick();
    //     assert(counter == 0);
    //     std::cout << "通过" << std::endl;
    // }

    // // 测试 3: 刷新任务延迟
    // {
    //     std::cout << "测试3: 刷新任务（延长执行时间） ... ";
    //     TimerWheel tw;
    //     int counter = 0;
    //     tw.AddTask(1, 2, [&counter] { ++counter; });

    //     tw.RunTick();          // tick=1
    //     tw.RefreshTask(1);     // 重新插入到 (1 + 2) % 60 = 3
    //     tw.RunTick();          // tick=2, 原来的 slot 清空但引用计数>0，不触发
    //     assert(counter == 0);

    //     tw.RunTick();          // tick=3, 刷新后的位置
    //     assert(counter == 1);
    //     assert(tw.MapSize() == 0);
    //     std::cout << "通过" << std::endl;
    // }

    // // 测试 4: 多个任务同一 slot
    // {
    //     std::cout << "测试4: 同一 slot 多个任务 ... ";
    //     TimerWheel tw;
    //     int a = 0, b = 0, c = 0;
    //     tw.AddTask(1, 1, [&a] { ++a; });
    //     tw.AddTask(2, 1, [&b] { ++b; });
    //     tw.AddTask(3, 1, [&c] { ++c; });

    //     tw.RunTick();  // tick=1
    //     assert(a == 1 && b == 1 && c == 1);
    //     assert(tw.MapSize() == 0);
    //     std::cout << "通过" << std::endl;
    // }

    // // 测试 5: 延迟为 0 的任务（需要走完一圈）
    // {
    //     std::cout << "测试5: 延迟为 0 的任务 ... ";
    //     TimerWheel tw;
    //     int counter = 0;
    //     tw.AddTask(1, 0, [&counter] { ++counter; });

    //     for (int i = 0; i < 60; ++i)
    //         tw.RunTick();
    //     assert(counter == 1);
    //     std::cout << "通过" << std::endl;
    // }

    // // 测试 6: 重复 ID 覆盖旧任务
    // {
    //     std::cout << "测试6: 重复 ID 覆盖（旧任务被取消，新任务正常执行） ... ";
    //     TimerWheel tw;
    //     int old_cb = 0, new_cb = 0;
    //     tw.AddTask(1, 3, [&old_cb] { ++old_cb; });
    //     tw.AddTask(1, 3, [&new_cb] { ++new_cb; });

    //     for (int i = 0; i < 3; ++i)
    //         tw.RunTick();

    //     assert(old_cb == 0);   // [FIX 8] 旧任务被取消，不执行
    //     assert(new_cb == 1);   // 新任务正常到期执行
    //     std::cout << "通过" << std::endl;
    // }

    // std::cout << "\n所有测试通过!" << std::endl;
    return 0;
}
