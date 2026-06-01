#pragma once
// ============================================================================
// 🏗️ ECS 管理器 — 数字生命培养皿
// ============================================================================
//
// ECSManager 是整个模拟的数据中枢。它同时管理：
//   1. 实体生命周期（创建/销毁/查询）
//   2. 组件数据的存储和访问
//   3. 三个内存池（程序/数据/栈）的生命周期
//
// 设计决策 — 为什么用数组而非哈希表？
//   - std::array 提供 O(1) 随机访问（通过实体 ID 直接索引）
//   - 缓存友好：所有组件的同一字段在内存中连续排列
//   - 无动态分配：构造时一次性分配 MAX_ENTITIES 个槽位
//   - 实体 ID 即数组索引，无需额外的 ID→指针 映射
//
// 实体 ID 回收机制：
//   - m_nextId 单调递增，直到达到 MAX_ENTITIES
//   - 销毁的实体 ID 进入 m_freeIds 空闲列表
//   - 创建新实体时优先从 m_freeIds 复用 ID
//   - 这确保 ID 空间得到充分利用，不会因为频繁创建/销毁而耗尽
//
// 组件存储模式 — SoA (Structure of Arrays)：
//   所有 GenomeComponent 存于 m_genomes[]，所有 EnergyComponent 存于 m_energies[]...
//   相比 AoS (Array of Structures) 的优势：
//     - 某个 System 只访问 EnergyComponent 时，不会加载无关数据到缓存
//     - 向量化友好（虽然这里没有用 SIMD）
//     - 更清晰的关注点分离
// ============================================================================

#include "config.h"
#include "components.h"
#include "memory_pool.h"
#include <array>
#include <vector>

class MemoryPool;  // 前向声明（实际定义在 memory_pool.h）

class ECSManager {
public:
    ECSManager();
    ~ECSManager();

    // ========================================================================
    // 实体管理
    // ========================================================================

    /// @brief 创建一个新实体（从空闲列表复用或分配新 ID）
    /// @return 新实体 ID，如果实体数已达上限则返回 Entity::INVALID (-1)
    int32_t createEntity();

    /// @brief 销毁实体（释放内存池块，标记为不活跃，回收 ID）
    void    destroyEntity(int32_t entityId);

    /// @brief 检查实体是否存活（active=true 且 alive=true）
    bool    isEntityAlive(int32_t entityId) const;

    /// @brief 获取当前活跃实体总数（包括已死亡但未清理的）
    int32_t getEntityCount() const { return m_entityCount; }

    /// @brief 获取当前存活的实体数量
    int32_t getAliveCount() const;

    /// @brief 获取实体引用（可修改）
    Entity&       getEntity(int32_t entityId);
    /// @brief 获取实体只读引用
    const Entity& getEntity(int32_t entityId) const;

    // ========================================================================
    // 组件存取（按实体 ID 索引，O(1) 随机访问）
    // ========================================================================

    GenomeComponent&   getGenome(int32_t entityId);    ///< 基因组（DNA程序代码）
    EnergyComponent&   getEnergy(int32_t entityId);    ///< 能量（生命货币）
    PositionComponent& getPosition(int32_t entityId);  ///< 位置（环面坐标）
    VMState&           getVMState(int32_t entityId);   ///< 虚拟机状态
    AgeComponent&      getAge(int32_t entityId);       ///< 年龄与代数
    AliveComponent&    getAlive(int32_t entityId);     ///< 存活标记
    SensorComponent&   getSensor(int32_t entityId);    ///< 传感器缓存

    /// @brief 检查实体是否拥有指定类型的组件
    bool hasComponent(int32_t entityId, ComponentType type) const;

    // ========================================================================
    // 迭代
    // ========================================================================

    /// @brief 遍历所有存活实体并执行回调
    /// @tparam Func 可调用对象 (int32_t entityId, Entity&) -> void
    /// @param func 对每个存活实体调用的函数
    template<typename Func>
    void forEachAlive(Func&& func) {
        for (int32_t i = 0; i < Config::MAX_ENTITIES; ++i) {
            if (m_entities[i].active && m_alive[i].alive) {
                func(i, m_entities[i]);
            }
        }
    }

    /// @brief 获取所有存活实体的 ID 列表（用于范围 for 循环）
    /// @return 存活实体 ID 的 vector（按 ID 升序）
    std::vector<int32_t> getAliveEntityIds() const;

    // ========================================================================
    // 内存池
    // ========================================================================

    /// @brief 程序内存池（存储基因组/指令）
    MemoryPool& getProgramMemory() { return *m_programPool; }
    /// @brief 数据内存池（通用读写存储）
    MemoryPool& getDataMemory()    { return *m_dataPool; }
    /// @brief 栈内存池（调用栈）
    MemoryPool& getStackMemory()   { return *m_stackPool; }

private:
    // ── 组件数组（SoA 布局，MAX_ENTITIES 个槽位） ────────────────────
    std::array<Entity,            Config::MAX_ENTITIES> m_entities;   ///< 实体元数据
    std::array<GenomeComponent,   Config::MAX_ENTITIES> m_genomes;   ///< 基因组组件
    std::array<EnergyComponent,   Config::MAX_ENTITIES> m_energies;  ///< 能量组件
    std::array<PositionComponent, Config::MAX_ENTITIES> m_positions; ///< 位置组件
    std::array<VMState,           Config::MAX_ENTITIES> m_vmStates;  ///< VM 状态组件
    std::array<AgeComponent,      Config::MAX_ENTITIES> m_ages;      ///< 年龄组件
    std::array<AliveComponent,    Config::MAX_ENTITIES> m_alive;     ///< 存活标记组件
    std::array<SensorComponent,   Config::MAX_ENTITIES> m_sensors;   ///< 传感器组件

    int32_t m_entityCount;           ///< 当前活跃实体槽位数

    // ── 内存池 ───────────────────────────────────────────────────────
    MemoryPool* m_programPool;       ///< 程序内存池（256B/块 × MAX_ENTITIES）
    MemoryPool* m_dataPool;          ///< 数据内存池（128B/块 × MAX_ENTITIES）
    MemoryPool* m_stackPool;         ///< 栈内存池（256B/块 × MAX_ENTITIES）

    // ── ID 分配 ──────────────────────────────────────────────────────
    int32_t m_nextId;                ///< 下一个新 ID（单调递增）
    std::vector<int32_t> m_freeIds;  ///< 回收的空闲 ID（LIFO 复用）
};
