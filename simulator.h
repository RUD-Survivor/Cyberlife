#pragma once
// ============================================================================
// 🎮 数字生命模拟器 (培养皿) — 顶层协调器
// ============================================================================
//
// DigitalLifeSimulator 是整个项目的顶层入口。它负责：
//   1. 持有 ECSManager、WorldState、随机数生成器
//   2. 组装 System 管线（setupSystems()）
//   3. 驱动 Tick 循环（tick() 按顺序调用所有 System）
//   4. 世界初始化（食物生成 + 种子生命创建）
//   5. 基因组生成（随机基因组 / 种子基因组）
//   6. 可视化输出（控制台渲染）
//
// 使用示例：
//   DigitalLifeSimulator sim;
//   std::mt19937 rng(42);
//   sim.initialize(rng);       // 初始化世界
//   for (int i = 0; i < 1000; ++i) sim.tick();  // 运行 1000 ticks
//   sim.renderStats(std::cout);  // 输出统计
// ============================================================================

#include "ecs_manager.h"
#include "world_state.h"
#include "rule_system.h"
#include "systems.h"
#include <random>
#include <vector>
#include <iostream>

class DigitalLifeSimulator {
public:
    DigitalLifeSimulator();
    ~DigitalLifeSimulator();

    // ========================================================================
    // 生命周期
    // ========================================================================

    /// @brief 初始化世界：生成食物 + 放置种子生命
    /// @param rng 随机数生成器（用于确定性的世界初始化）
    void initialize(std::mt19937& rng);

    /// @brief 执行一个 Tick（调用全部 8 个 System）
    void tick();

    /// @brief 批量运行 n 个 Tick
    /// @param numTicks 运行的 tick 数
    /// @param verbose  是否每 10 tick 输出状态
    void run(int32_t numTicks, bool verbose = false);

    // ========================================================================
    // 生命创建
    // ========================================================================

    /// @brief 在指定位置创建一个原始生命（第 0 代）
    /// @param x, y  出生坐标（会被环面包裹）
    /// @param genome 基因组程序代码
    /// @return 新实体 ID，失败返回 -1
    int32_t spawnPrimordialLife(std::mt19937& rng,
                                int32_t x, int32_t y,
                                const std::vector<uint8_t>& genome);

    // ========================================================================
    // 基因组生成（静态工具方法）
    // ========================================================================

    /// @brief 生成完全随机的基因组
    /// @param rng    随机数生成器
    /// @param length 基因组长度（字节），默认 64
    /// @return 随机字节序列
    static std::vector<uint8_t> generateRandomGenome(std::mt19937& rng,
                                                      size_t length = 64);

    /// @brief 生成"种子基因组"——硬编码的初始行为程序
    /// @details 种子基因组包含一个简单的生存策略循环：
    ///          SENSE → 检查能量 → 低能则进食 → 中能则战斗 → 高能则复制 → 随机移动
    ///          这是所有进化的起点。
    static std::vector<uint8_t> generateSeedGenome(std::mt19937& rng);

    // ========================================================================
    // 状态存取
    // ========================================================================

    ECSManager&       getECS()       { return m_ecs; }
    const ECSManager& getECS() const { return m_ecs; }
    WorldState&       getWorld()       { return m_world; }
    const WorldState& getWorld() const { return m_world; }
    std::mt19937&     getRNG()         { return m_rng; }

    // ========================================================================
    // 可视化（控制台输出）
    // ========================================================================

    /// @brief ASCII 渲染世界网格（简洁）@=高能 O=中能 o=低能 .=濒死 #=食物
    void render(std::ostream& out);

    /// @brief 详细渲染 = 世界网格 + 统计数据
    void renderDetailed(std::ostream& out);

    /// @brief 输出全局统计（tick数、活体数、能量、代数分布等）
    void renderStats(std::ostream& out);

    /// @brief 输出单个实体的详细信息（寄存器、能量、传感器等）
    void renderEntityInfo(std::ostream& out, int32_t entityId);

private:
    ECSManager m_ecs;               ///< 实体组件管理器
    WorldState m_world;             ///< 世界状态（网格+统计）
    std::mt19937 m_rng;             ///< Mersenne Twister 随机数生成器

    std::vector<SystemBase*> m_systems;  ///< System 管线（8 个系统）

    /// @brief 初始化 System 管线（按执行顺序注册）
    void setupSystems();
};
