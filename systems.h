#pragma once
// ============================================================================
// 🔄 ECS System 管线 — 数字生命培养皿
// ============================================================================
//
// System 管线定义了每个 Tick 内所有系统的执行顺序。
// 这个顺序经过仔细设计，确保因果关系正确：
//
//   Tick 执行流（8 个阶段）：
//   ┌─────────────┐
//   │ 1.Metabolism │  每实体能量衰减（年龄越大衰减越快）
//   │   能量衰减    │  能量耗尽 → 标记死亡 + 能量化为食物
//   └──────┬───────┘
//          ▼
//   ┌─────────────┐
//   │ 2.Sense      │  填充每个实体的 SensorComponent
//   │   环境感知    │  探测 SENSE_RANGE 内实体 + 当前位置食物
//   └──────┬───────┘
//          ▼
//   ┌─────────────┐
//   │ 3.VMExecute  │  每个实体执行 TICKS_PER_STEP 条指令
//   │   虚拟机执行  │  VM 指令可能触发: MOVE/EAT/FIGHT/REPLICATE/SENSE/DIE
//   └──────┬───────┘
//          ▼
//   ┌─────────────┐
//   │ 4.Movement   │  重建 entityGrid（从 Position 组件反向填充）
//   │   网格同步    │  解决 VM 执行期间可能造成的网格不一致
//   └──────┬───────┘
//          ▼
//   ┌─────────────┐
//   │ 5.Eating     │  (由 VM EAT 指令驱动，本系统为空操作)
//   │   进食        │
//   └──────┬───────┘
//          ▼
//   ┌─────────────┐
//   │ 6.Combat     │  (由 VM FIGHT 指令驱动，本系统为空操作)
//   │   战斗        │
//   └──────┬───────┘
//          ▼
//   ┌─────────────┐
//   │ 7.Replication│  (由 VM REPLICATE 指令驱动，本系统为空操作)
//   │   自我复制    │
//   └──────┬───────┘
//          ▼
//   ┌─────────────┐
//   │ 8.Death      │  老死判定 + 能量耗尽清理 + 销毁死亡实体
//   │   死亡清理    │  释放内存池 + 回收实体 ID
//   └─────────────┘
//
// 为什么 Eating/Combat/Replication 的 execute() 是空的？
//   因为这些操作由 VM 指令直接驱动（EAT/FIGHT/REPLICATE），在 VMExecute
//   阶段已经完成。保留独立的 System 类是为了架构一致性和未来的批量操作扩展。
// ============================================================================

#include "ecs_manager.h"
#include "world_state.h"
#include <random>

// ============================================================================
// System 基类 — 所有系统的抽象接口
// ============================================================================
class SystemBase {
public:
    virtual ~SystemBase() = default;

    /// @brief 执行系统逻辑
    /// @param ecs   实体组件管理器（读写）
    /// @param world 世界状态（读写）
    /// @param rng   随机数生成器（可用于随机化行为）
    virtual void execute(ECSManager& ecs, WorldState& world,
                         std::mt19937& rng) = 0;

    /// @brief 系统名称（用于调试输出）
    virtual const char* name() const = 0;
};

// ============================================================================
// 1️⃣ 代谢系统 — 能量衰减 + 能量耗尽死亡
// ============================================================================
// 每 tick 对每个存活实体执行能量衰减。衰减量随年龄增加（模拟衰老）。
// 能量 ≤ 0 的实体被标记死亡，剩余能量转化为食物洒在原地。
// ============================================================================
class MetabolismSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Metabolism"; }
};

// ============================================================================
// 2️⃣ 感知系统 — 填充传感器缓存
// ============================================================================
// 对每个存活实体，扫描 SENSE_RANGE 范围内的其他实体和食物。
// 结果写入 SensorComponent，供 VM 的 SENSE 指令读取。
// 复杂度 O(n²) where n = 存活实体数（最坏情况）。
// ============================================================================
class SenseSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Sense"; }
};

// ============================================================================
// 3️⃣ VM 执行系统 — 运行基因组程序
// ============================================================================
// 对每个存活实体，从其基因组中取指、解码、执行。
// 每实体每 tick 最多执行 TICKS_PER_STEP 条指令。
// 支持 32 条通用指令 + 6 条数字生命专用指令。
// 这是整个模拟的"大脑"——也是计算量最大的 System。
// ============================================================================
class VMExecutionSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "VMExecution"; }

private:
    /// @brief 执行单条 VM 指令
    /// @return true=继续执行, false=停机(实体 halted 或死亡)
    bool    executeInstruction(ECSManager& ecs, WorldState& world,
                               int32_t entityId, std::mt19937& rng);

    /// @brief 从基因组取指（读取当前 IP 指向的操作码）
    uint8_t fetchOpcode(VMState& vm);

    /// @brief 读取可变长立即数（1-2 字节，支持 7 位和 14 位）
    int32_t fetchImmediate(VMState& vm);
};

// ============================================================================
// 4️⃣ 移动系统 — 实体网格位置一致性维护
// ============================================================================
// 重建 entityGrid：遍历所有存活实体的 Position 组件，
// 将它们填入网格。这修正了 VM 执行期间 MOVE 操作可能造成的
// Position 组件与 entityGrid 不一致。
// 遇到冲突时（两个实体占据同一格），将冲突实体随机重新放置。
// ============================================================================
class MovementSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Movement"; }

    /// @brief 移动单个实体（由 VM 的 MOVE 指令调用）
    /// @param direction 移动方向 0=上 1=右 2=下 3=左
    /// @return true=移动成功, false=目标被占用/能量不足
    static bool moveEntity(ECSManager& ecs, WorldState& world,
                           int32_t entityId, int32_t direction);
};

// ============================================================================
// 5️⃣ 进食系统 — 消耗环境食物获取能量
// ============================================================================
// 由 VM 的 EAT 指令直接驱动（在 VMExecute 阶段完成）。
// execute() 为空 — 保留框架以支持未来可能的批量进食操作。
// ============================================================================
class EatingSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Eating"; }

    /// @brief 在当前位置进食（由 VM EAT 指令调用）
    /// @return true=进食成功（有食物）, false=无食物
    static bool eatAtPosition(ECSManager& ecs, WorldState& world,
                              int32_t entityId);
};

// ============================================================================
// 6️⃣ 战斗系统 — 实体间能量争夺
// ============================================================================
// 由 VM 的 FIGHT 指令直接驱动（在 VMExecute 阶段完成）。
// execute() 为空。
// ============================================================================
class CombatSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Combat"; }

    /// @brief 向目标实体发起战斗（由 VM FIGHT 指令调用）
    /// @return true=战斗有效执行, false=目标无效/能量不足
    static bool fightEntity(ECSManager& ecs, WorldState& world,
                            int32_t attackerId, int32_t targetId);
};

// ============================================================================
// 7️⃣ 复制系统 — 自我复制 + 基因变异
// ============================================================================
// 由 VM 的 REPLICATE 指令直接驱动（在 VMExecute 阶段完成）。
// execute() 为空。
// ============================================================================
class ReplicationSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Replication"; }

    /// @brief 执行自我复制（由 VM REPLICATE 指令调用）
    /// @return 子实体 ID，失败返回 -1
    /// @note 子实体基因会发生变异（点突变+插入+删除）
    static int32_t replicateEntity(ECSManager& ecs, WorldState& world,
                                   int32_t parentId, std::mt19937& rng);
};

// ============================================================================
// 8️⃣ 死亡系统 — 老死判定 + 能量耗尽清理 + 销毁
// ============================================================================
// 处理两类死亡：
//   1. 老死 — 年龄超过 maxAgeForGeneration(generation)
//   2. 能量耗尽 — energy ≤ 0
// 死亡实体的剩余能量转化为食物，然后从世界中移除。
// 最后统一销毁所有标记为死亡的实体（释放内存池 + 回收 ID）。
// ============================================================================
class DeathSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Death"; }
};
