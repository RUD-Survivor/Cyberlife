// ============================================================================
// ⚖️ 法则系统 — 实现
// ============================================================================
// 本文件实现了数字生命世界的全部"物理定律"。
// 每条法则都经过校准，以产生有趣且多样化的进化动力学。
// 核心设计目标：能量守恒、压力诱导变异、边际收益递减、代数优势。
// ============================================================================
#include "rule_system.h"
#include "config.h"  // DL_CLAMP, Config::* constants
#include <cmath>
#include <algorithm>

// ============================================================================
// ⚡ 能量法则
// ============================================================================

int32_t RuleSystem::calculateDecay(int32_t energy, int32_t age) {
    // 年龄因子：年轻~1.0 → 年老~3.0
    // 模拟衰老导致的代谢效率下降
    double af = 1.0 + (static_cast<double>(age) / Config::MAX_AGE) * 2.0;
    return static_cast<int32_t>(Config::ENERGY_DECAY_RATE * af);
}

int32_t RuleSystem::calculateReplicationCost(int32_t parentEnergy) {
    // 基础 50 + 父体能量的 20%
    // 高能实体复制代价更大 → 防止"富人"无限复制
    return Config::REPLICATION_COST + static_cast<int32_t>(parentEnergy * 0.2);
}

int32_t RuleSystem::calculateFightResult(int32_t atk, int32_t def) {
    // 旧版战斗公式（保留兼容性）
    // 能量优势方获得更多战利品
    return static_cast<int32_t>((atk > def) ? def * 0.3 : def * 0.1);
}

int32_t RuleSystem::calculateEnergyGainFromFood(int32_t food) {
    // 单次进食上限为 FOOD_ENERGY_PER_BITE
    return std::min(food, Config::FOOD_ENERGY_PER_BITE);
}

// ============================================================================
// 🧬 变异法则
// ============================================================================

double RuleSystem::effectiveMutationRate(int32_t gen, int32_t energy) {
    // 能量因子：低能量 → 高变异（最高 4 倍）— 压力诱导变异假说
    double ef = 1.0 + (1.0 - DL_CLAMP(static_cast<double>(energy) / Config::MAX_ENERGY, 0.0, 1.0)) * 3.0;
    // 代数因子：高代数 → 略高变异 — 进化加速
    double gf = 1.0 + std::log(1.0 + gen) * 0.5;
    return Config::MUTATION_RATE * ef * gf;
}

void RuleSystem::applyMutations(std::vector<uint8_t>& genome, std::mt19937& rng,
                                 int32_t gen, int32_t energy) {
    double rate = effectiveMutationRate(gen, energy);  // 计算有效变异率
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int32_t> bd(0, 255);  // 随机字节

    // 1) 点突变：每个字节以 rate 概率翻转一个随机位
    for (size_t i = 0; i < genome.size(); ++i)
        if (prob(rng) < rate)
            genome[i] ^= (1 << std::uniform_int_distribution<int>(0, 7)(rng));

    // 2) 插入突变：在随机位置插入一个随机字节
    if (prob(rng) < Config::INSERTION_RATE && genome.size() < static_cast<size_t>(Config::PROGRAM_MEMORY_SIZE)) {
        genome.insert(genome.begin() + std::uniform_int_distribution<size_t>(0, genome.size())(rng),
// ============================================================================
// ⌛ 年龄法则
// ============================================================================

bool RuleSystem::shouldDieOfAge(int32_t age, int32_t gen) { return age >= maxAgeForGeneration(gen); }

int32_t RuleSystem::maxAgeForGeneration(int32_t gen) {
    // 每代增加 50 ticks 寿命：G0=2000, G1=2050, ..., G100=7000
    return Config::MAX_AGE + gen * 50;
}

// ============================================================================
// ⚔️ 战斗法则 — 基于能量比的概率判定
// ============================================================================

bool RuleSystem::fightResolve(int32_t atk, int32_t def, int32_t& transfer, bool& atkWins) {
    double total = static_cast<double>(atk) + def;

    // 双方无能量 → 无效战斗
    if (total <= 0) { atkWins = false; transfer = 0; return false; }

    // 胜率 = atk/total，>0.5 则攻击方获胜（确定性近似）
    atkWins = (atk / total > 0.5);

    // 胜者获得防御者 70% 能量，败者仅 30%
    transfer = static_cast<int32_t>(atkWins ? def * 0.7 : def * 0.3);
    return true;
}

// ============================================================================
// ✅ 条件检查 — 各行为的前置条件
// ============================================================================

bool RuleSystem::canReplicate(int32_t e, int32_t, int32_t gen, int32_t nearby) {
    // 能量≥阈值 AND 代数<上限 AND 不拥挤（<10 邻居）

void RuleSystem::applyMutations(std::vector<uint8_t>& genome, std::mt19937& rng,
                                 int32_t gen, int32_t energy) {
    double rate = effectiveMutationRate(gen, energy);
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int32_t> bd(0, 255);

    for (size_t i = 0; i < genome.size(); ++i)
        if (prob(rng) < rate) genome[i] ^= (1 << std::uniform_int_distribution<int>(0, 7)(rng));

    if (prob(rng) < Config::INSERTION_RATE && genome.size() < static_cast<size_t>(Config::PROGRAM_MEMORY_SIZE)) {
        genome.insert(genome.begin() + std::uniform_int_distribution<size_t>(0, genome.size())(rng),
                      static_cast<uint8_t>(bd(rng)));
    }
    if (prob(rng) < Config::DELETION_RATE && genome.size() > 8) {
        genome.erase(genome.begin() + std::uniform_int_distribution<size_t>(0, genome.size() - 1)(rng));
    }
    if (genome.size() > static_cast<size_t>(Config::PROGRAM_MEMORY_SIZE))
        genome.resize(Config::PROGRAM_MEMORY_SIZE);
}

bool RuleSystem::shouldDieOfAge(int32_t age, int32_t gen) { return age >= maxAgeForGeneration(gen); }
int32_t RuleSystem::maxAgeForGeneration(int32_t gen) { return Config::MAX_AGE + gen * 50; }

bool RuleSystem::fightResolve(int32_t atk, int32_t def, int32_t& transfer, bool& atkWins) {
    double total = static_cast<double>(atk) + def;
    if (total <= 0) { atkWins = false; transfer = 0; return false; }
    atkWins = (atk / total > 0.5);
    transfer = static_cast<int32_t>(atkWins ? def * 0.7 : def * 0.3);
    return true;
}

bool RuleSystem::canReplicate(int32_t e, int32_t, int32_t gen, int32_t nearby) {
    return e >= Config::REPLICATION_THRESHOLD && gen < Config::MAX_GENERATION && nearby < 10;
}
bool RuleSystem::canFight(int32_t e)  { return e >= Config::FIGHT_ENERGY_COST * 2; }
bool RuleSystem::canMove(int32_t e)   { return e >= Config::MOVE_ENERGY_COST; }
bool RuleSystem::canSense(int32_t e)  { return e >= Config::SENSE_ENERGY_COST; }
