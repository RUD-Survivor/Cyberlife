#pragma once
// ============================================================================
// 🌍 世界状态 — 数字生命培养皿
// ============================================================================
//
// 世界状态维护两个核心网格和全局统计：
//
// ┌───────────────────────────────────────────────────────┐
// │ foodGrid[y][x]    — 每格的当前食物量 (0~MAX_FOOD)   │
// │ entityGrid[y][x]  — 每格占用的实体 ID (-1=空)       │
// └───────────────────────────────────────────────────────┘
//
// 双网格设计：
//   食物和实体分开存储，因为它们有不同的更新频率和语义。
//   实体可以站在食物上——同一格既有实体 ID 又有食物值。
//
// 环面拓扑：
//   所有坐标访问自动通过 DL_WRAP_COORD 处理边界包装，
//   确保 (WORLD_WIDTH, WORLD_HEIGHT) 外的坐标也能正确映射。
//
// 全局统计：
//   所有 total* 计数器是 uint64_t——即使长期运行也不会溢出。
//   这些统计用于分析模拟的宏观趋势。
//
// entityGrid 的一致性：
//   MovementSystem 每 tick 重建 entityGrid（从所有实体的位置组件反向填充），
//   以修正可能的不一致（如实体被移动后网格未更新）。
// ============================================================================

#include "config.h"
#include <cstdint>
#include <random>

struct WorldState {
    // ── 双网格系统 ───────────────────────────────────────────────────
    int32_t foodGrid[Config::WORLD_HEIGHT][Config::WORLD_WIDTH];   ///< 食物分布网格
    int32_t entityGrid[Config::WORLD_HEIGHT][Config::WORLD_WIDTH]; ///< 实体占用网格（-1=空）

    // ── 时间 ─────────────────────────────────────────────────────────
    uint64_t tickCount;  ///< 当前 tick 编号（从 0 开始递增）

    // ── 全局统计计数器 ─────────────────────────────────────────────
    uint64_t totalBirths;        ///< 累计出生次数
    uint64_t totalDeaths;        ///< 累计死亡次数
    uint64_t totalFights;        ///< 累计战斗次数
    uint64_t totalReplications;  ///< 累计复制次数
    uint64_t totalMutations;     ///< 累计发生变异的复制次数
    uint64_t totalFoodSpawned;   ///< 累计生成的食物总能量
    uint64_t totalFoodConsumed;  ///< 累计消耗的食物总能量

    /// @brief 构造并重置世界状态
    WorldState();

    /// @brief 完全重置：清零网格和所有统计计数器
    void reset();

    /// @brief 在世界上随机生成食物（每 tick 调用一次）
    /// @param rng 随机数生成器
    /// @details 生成约 WORLD_WIDTH*WORLD_HEIGHT*FOOD_SPAWN_RATE 个食物单位
    void spawnFood(std::mt19937& rng);

    // ── 食物操作 ────────────────────────────────────────────────────

    /// @brief 检查指定坐标是否被实体占用
    bool    isOccupied(int32_t x, int32_t y) const;

    /// @brief 获取指定坐标的食物量
    int32_t getFood(int32_t x, int32_t y) const;

    /// @brief 消耗指定坐标的食物（进食）
    /// @param amount 尝试消耗的量
    /// @return 实际消耗的量（不超过该格现有食物）
    int32_t consumeFood(int32_t x, int32_t y, int32_t amount);

    /// @brief 向指定坐标添加食物（死亡后能量转化 / 食物生成）
    /// @param amount 添加量，超过 MAX_FOOD_PER_TILE 会被截断
    void    addFood(int32_t x, int32_t y, int32_t amount);

    // ── 实体网格操作 ────────────────────────────────────────────────

    /// @brief 在指定坐标放置实体
    void    setEntity(int32_t x, int32_t y, int32_t entityId);

    /// @brief 清除指定坐标的实体占用（设为 -1）
    void    clearEntity(int32_t x, int32_t y);

    /// @brief 获取指定坐标的实体 ID（-1=无实体）
    int32_t getEntity(int32_t x, int32_t y) const;
};
