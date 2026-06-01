#pragma once
// ============================================================================
// ⚖️ 法则系统 — 数字生命培养皿
// ============================================================================
//
// RuleSystem 是模拟世界的"物理定律"。所有 System 在执行操作前
// 都必须通过 RuleSystem 进行合法性检查和结果计算。
//
// 设计原则：
//   - 所有方法均为静态 — RuleSystem 是无状态的纯函数集合
//   - 法则集中管理 — 修改"游戏规则"只需改这一处
//   - 确定性 — 相同输入产生相同输出（不依赖随机数，变异除外）
//
// 法则分类：
//   ┌──────────┬────────────────────────────────────────────┐
//   │ 能量法则  │ 代谢衰减、复制代价、战斗转移、进食收益   │
//   │ 变异法则  │ 突变率计算（能量+代数的函数）、基因修改  │
//   │ 年龄法则  │ 最大寿命计算、老死判定                   │
//   │ 战斗法则  │ 胜负判定、能量转移量                     │
//   │ 条件检查  │ 各行为的前置条件（能量阈值等）           │
//   └──────────┴────────────────────────────────────────────┘
// ============================================================================

#include <cstdint>
#include <vector>
#include <random>

struct RuleSystem {
    // ========================================================================
    // ⚡ 能量法则 — 能量收支的数学规则
    // ========================================================================

    /// @brief 计算每 tick 的代谢衰减量
    /// @param energy 当前能量（保留参数，当前实现不使用）
    /// @param age    当前年龄
    /// @return 衰减量 = ENERGY_DECAY_RATE * (1 + (age/MAX_AGE)*2)
    /// @note 年龄越大衰减越快，模拟衰老导致的代谢效率下降
    static int32_t calculateDecay(int32_t energy, int32_t age);

    /// @brief 计算复制的能量消耗
    /// @param parentEnergy 父体当前能量
    /// @return 消耗 = REPLICATION_COST + parentEnergy * 0.2
    /// @note 高能实体复制代价更大，促进资源代际再分配
    static int32_t calculateReplicationCost(int32_t parentEnergy);

    /// @brief 计算战斗的能量转移量（旧版，保留兼容）
    /// @deprecated 请使用 fightResolve()
    static int32_t calculateFightResult(int32_t attackerEnergy, int32_t defenderEnergy);

    /// @brief 计算进食可获取的能量
    /// @param foodAmount 所在格的食物量
    /// @return min(foodAmount, FOOD_ENERGY_PER_BITE)
    static int32_t calculateEnergyGainFromFood(int32_t foodAmount);

    // ========================================================================
    // 🧬 变异法则 — 进化的数学引擎
    // ========================================================================

    /// @brief 计算有效变异率（考虑能量压力和代数）
    /// @param generation 当前代数
    /// @param energy     当前能量
    /// @return 有效变异率 = MUTATION_RATE * energyFactor * generationFactor
    ///         - energyFactor = 1 + (1 - energyRatio) * 3  （低能→高变异，最高4倍）
    ///         - generationFactor = 1 + log(1 + generation) * 0.5
    static double effectiveMutationRate(int32_t generation, int32_t energy);

    /// @brief 对基因组应用随机变异（点突变 + 插入 + 删除）
    /// @param genome     待变异的基因组（原地修改）
    /// @param rng        随机数生成器
    /// @param generation 父代代数
    /// @param energy     父体能量
    /// @note 变异类型：1) 位翻转(point) 2) 插入(insertion) 3) 删除(deletion)
    static void   applyMutations(std::vector<uint8_t>& genome, std::mt19937& rng,
                                 int32_t generation, int32_t energy);

    // ========================================================================
    // ⌛ 年龄法则 — 寿命的数学规则
    // ========================================================================

    /// @brief 判定实体是否应因年老而死
    /// @param age        当前年龄 (ticks)
    /// @param generation 当前代数
    /// @return age >= maxAgeForGeneration(generation)
    static bool    shouldDieOfAge(int32_t age, int32_t generation);

    /// @brief 计算给定代数的最大寿命
    /// @param generation 代数
    /// @return MAX_AGE + generation * 50
    /// @note 后代寿命更长——模拟"长寿基因"的自然选择
    static int32_t maxAgeForGeneration(int32_t generation);

    // ========================================================================
    // ⚔️ 战斗法则 — 能量争夺的数学规则
    // ========================================================================

    /// @brief 结算战斗结果
    /// @param attackerEnergy 攻击者当前能量（输入）
    /// @param defenderEnergy 防御者当前能量（输入）
    /// @param energyTransfer 能量转移量（输出）— 从防御者转移给攻击者
    /// @param attackerWins   攻击者是否获胜（输出）
    /// @return 战斗是否有效（双方能量和 > 0）
    /// @note 基于能量的概率判定：胜率 = atkEnergy / (atkEnergy + defEnergy)
    static bool fightResolve(int32_t attackerEnergy, int32_t defenderEnergy,
                             int32_t& energyTransfer, bool& attackerWins);

    // ========================================================================
    // ✅ 条件检查 — 行为的前置条件
    // ========================================================================

    /// @brief 检查是否可以复制
    /// @param energy      当前能量
    /// @param age         当前年龄（保留，当前未使用）
    /// @param generation  当前代数
    /// @param nearbyCount 附近的实体数量
    /// @return 能量≥阈值 且 代数<最大代数 且 附近实体<10
    static bool canReplicate(int32_t energy, int32_t age, int32_t generation,
                             int32_t nearbyCount);

    /// @brief 检查是否可以战斗（能量 ≥ FIGHT_ENERGY_COST * 2）
    static bool canFight(int32_t energy);

    /// @brief 检查是否可以移动（能量 ≥ MOVE_ENERGY_COST）
    static bool canMove(int32_t energy);

    /// @brief 检查是否可以感知（能量 ≥ SENSE_ENERGY_COST）
    static bool canSense(int32_t energy);
};
