// ============================================================================
// 🧬 数字生命培养皿 — 整体版实现文件
// ============================================================================
//
// ⚠️ 重要说明：
//   本文件是整个项目的"整体版"（monolithic）实现，包含所有模块的完整代码。
//   项目同时提供了模块化版本（每个模块独立 .cpp 文件），功能完全一致。
//   模块化版本的文件列表见 digital_life_vm.h 顶部注释。
//
// 实现顺序：
//   辅助宏 → MemoryPool → VMState → 组件 → WorldState → RuleSystem
//   → ECSManager → MetabolismSystem → SenseSystem → VMExecutionSystem
//   → MovementSystem → EatingSystem → CombatSystem → ReplicationSystem
//   → DeathSystem → DigitalLifeSimulator
// ============================================================================

#include "digital_life_vm.h"
#include <cmath>
#include <cassert>
#include <chrono>
#include <map>
#include <set>

// ============================================================================
// 🔧 辅助宏 — 环面坐标包装 + 数值钳制
// ============================================================================
#define CLAMP(x, lo, hi) (((x) < (lo)) ? (lo) : (((x) > (hi)) ? (hi) : (x)))
#define WRAP_COORD(v, max) (((v) % (max) + (max)) % (max))

// ============================================================================
// 🗂️ MemoryPool 实现 — LIFO 空闲链表分配器
// ============================================================================
MemoryPool::MemoryPool(size_t blockSize, size_t maxBlocks)
    : m_blockSize(blockSize)
    , m_maxBlocks(maxBlocks)
    , m_usedCount(0)
    , m_memory(nullptr)
{
    size_t totalSize = blockSize * maxBlocks;
    m_memory = new uint8_t[totalSize];
    std::memset(m_memory, 0, totalSize);

    // 初始化空闲链表 (预分配所有块)
    m_freeList.reserve(maxBlocks);
    for (int32_t i = static_cast<int32_t>(maxBlocks) - 1; i >= 0; --i) {
        m_freeList.push_back(i);
    }
}

MemoryPool::~MemoryPool() {
    delete[] m_memory;
    m_memory = nullptr;
}

int32_t MemoryPool::allocate() {
    if (m_freeList.empty()) return -1;
    int32_t idx = m_freeList.back();
    m_freeList.pop_back();
    ++m_usedCount;
    // 清零
    std::memset(m_memory + idx * m_blockSize, 0, m_blockSize);
    return idx;
}

void MemoryPool::deallocate(int32_t blockIndex) {
    if (blockIndex < 0 || blockIndex >= static_cast<int32_t>(m_maxBlocks))
        return;
    m_freeList.push_back(blockIndex);
    if (m_usedCount > 0) --m_usedCount;
}

uint8_t* MemoryPool::getBlock(int32_t blockIndex) {
    if (blockIndex < 0 || blockIndex >= static_cast<int32_t>(m_maxBlocks))
        return nullptr;
    return m_memory + blockIndex * m_blockSize;
}

void MemoryPool::clear() {
    m_freeList.clear();
    m_usedCount = 0;
    for (int32_t i = static_cast<int32_t>(m_maxBlocks) - 1; i >= 0; --i) {
        m_freeList.push_back(i);
    }
    std::memset(m_memory, 0, m_blockSize * m_maxBlocks);
}

// ============================================================================
// 🖥️ VMState 实现 — 虚拟机初始化、重置、反汇编
// ============================================================================
VMState::VMState()
    : ip(0), sp(0), flags(0)
    , programMemoryBlock(-1)
    , dataMemoryBlock(-1)
    , stackMemoryBlock(-1)
    , halted(false)
{
    reset();
}

void VMState::reset() {
    for (int i = 0; i < Config::NUM_REGISTERS; ++i) {
        regs[i] = 0;
    }
    ip = 0;
    sp = 0;
    flags = 0;
    halted = false;
}

void VMState::loadGenome(const std::vector<uint8_t>& code) {
    genome = code;
    reset();
}

std::string VMState::disassemble() const {
    std::stringstream ss;
    ss << "Registers: ";
    for (int i = 0; i < 4; ++i) {
        ss << "R" << i << "=" << std::setw(5) << regs[i] << " ";
    }
    ss << "\n           ";
    for (int i = 4; i < 8; ++i) {
        ss << "R" << i << "=" << std::setw(5) << regs[i] << " ";
    }
    ss << "\nIP=" << ip << " SP=" << sp
       << " FLAGS=" << std::bitset<8>(flags)
       << " HALTED=" << (halted ? "Y" : "N");
    return ss.str();
}

// ============================================================================
// 🧩 组件实现 — 基因组、能量、位置
// ============================================================================
uint32_t GenomeComponent::computeChecksum() const {
    uint32_t sum = 0;
    for (size_t i = 0; i < code.size(); ++i) {
        sum = sum * 31 + code[i];
    }
    return sum;
}

void EnergyComponent::spend(int32_t amount) {
    energy -= amount;
    energySpentThisTick += amount;
}

void EnergyComponent::gain(int32_t amount) {
    energy += amount;
    if (energy > maxEnergy) energy = maxEnergy;
}

void PositionComponent::move(int32_t direction) {
    direction = direction & 3; // 0-3
    switch (direction) {
        case 0: y = WRAP_COORD(y - 1, Config::WORLD_HEIGHT); break;
        case 1: x = WRAP_COORD(x + 1, Config::WORLD_WIDTH);  break;
        case 2: y = WRAP_COORD(y + 1, Config::WORLD_HEIGHT); break;
        case 3: x = WRAP_COORD(x - 1, Config::WORLD_WIDTH);  break;
    }
    facing = direction;
}

int32_t PositionComponent::distanceTo(const PositionComponent& other) const {
    // 环面距离
    int32_t dx = std::abs(x - other.x);
    int32_t dy = std::abs(y - other.y);
    dx = std::min(dx, Config::WORLD_WIDTH - dx);
    dy = std::min(dy, Config::WORLD_HEIGHT - dy);
    return std::max(dx, dy); // Chebyshev 距离
}

int32_t PositionComponent::directionTo(const PositionComponent& other) const {
    int32_t dx = other.x - x;
    int32_t dy = other.y - y;
    // 环面方向
    if (std::abs(dx) > Config::WORLD_WIDTH / 2)  dx = -dx;
    if (std::abs(dy) > Config::WORLD_HEIGHT / 2) dy = -dy;

    if (std::abs(dx) > std::abs(dy)) {
        return (dx > 0) ? 1 : 3; // 右:左
    } else {
        return (dy > 0) ? 2 : 0; // 下:上
    }
}

// ============================================================================
// 🌍 WorldState 实现 — 食物生成、环面坐标、实体网格
// ============================================================================
WorldState::WorldState()
    : tickCount(0)
    , totalBirths(0), totalDeaths(0), totalFights(0)
    , totalReplications(0), totalMutations(0)
    , totalFoodSpawned(0), totalFoodConsumed(0)
{
    reset();
}

void WorldState::reset() {
    for (int y = 0; y < Config::WORLD_HEIGHT; ++y) {
        for (int x = 0; x < Config::WORLD_WIDTH; ++x) {
            foodGrid[y][x] = 0;
            entityGrid[y][x] = -1;
        }
    }
    tickCount = 0;
    totalBirths = 0;
    totalDeaths = 0;
    totalFights = 0;
    totalReplications = 0;
    totalMutations = 0;
    totalFoodSpawned = 0;
    totalFoodConsumed = 0;
}

void WorldState::spawnFood(std::mt19937& rng) {
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int32_t> xDist(0, Config::WORLD_WIDTH - 1);
    std::uniform_int_distribution<int32_t> yDist(0, Config::WORLD_HEIGHT - 1);
    std::uniform_int_distribution<int32_t> amount(5, Config::MAX_FOOD_ENERGY);

    int32_t spawnCount = static_cast<int32_t>(
        Config::WORLD_WIDTH * Config::WORLD_HEIGHT * Config::FOOD_SPAWN_RATE);
    spawnCount = std::max(1, spawnCount);

    for (int i = 0; i < spawnCount; ++i) {
        int32_t fx = xDist(rng);
        int32_t fy = yDist(rng);
        int32_t famount = amount(rng);
        foodGrid[fy][fx] = std::min(foodGrid[fy][fx] + famount,
                                     Config::MAX_FOOD_PER_TILE);
        totalFoodSpawned += famount;
    }
}

bool WorldState::isOccupied(int32_t x, int32_t y) const {
    x = WRAP_COORD(x, Config::WORLD_WIDTH);
    y = WRAP_COORD(y, Config::WORLD_HEIGHT);
    return entityGrid[y][x] >= 0;
}

int32_t WorldState::getFood(int32_t x, int32_t y) const {
    x = WRAP_COORD(x, Config::WORLD_WIDTH);
    y = WRAP_COORD(y, Config::WORLD_HEIGHT);
    return foodGrid[y][x];
}

int32_t WorldState::consumeFood(int32_t x, int32_t y, int32_t amount) {
    x = WRAP_COORD(x, Config::WORLD_WIDTH);
    y = WRAP_COORD(y, Config::WORLD_HEIGHT);
    int32_t consumed = std::min(amount, foodGrid[y][x]);
    foodGrid[y][x] -= consumed;
    totalFoodConsumed += consumed;
    return consumed;
}

void WorldState::addFood(int32_t x, int32_t y, int32_t amount) {
    x = WRAP_COORD(x, Config::WORLD_WIDTH);
    y = WRAP_COORD(y, Config::WORLD_HEIGHT);
    foodGrid[y][x] = std::min(foodGrid[y][x] + amount, Config::MAX_FOOD_PER_TILE);
}

void WorldState::setEntity(int32_t x, int32_t y, int32_t entityId) {
    x = WRAP_COORD(x, Config::WORLD_WIDTH);
    y = WRAP_COORD(y, Config::WORLD_HEIGHT);
    entityGrid[y][x] = entityId;
}

void WorldState::clearEntity(int32_t x, int32_t y) {
    x = WRAP_COORD(x, Config::WORLD_WIDTH);
    y = WRAP_COORD(y, Config::WORLD_HEIGHT);
    entityGrid[y][x] = -1;
}

int32_t WorldState::getEntity(int32_t x, int32_t y) const {
    x = WRAP_COORD(x, Config::WORLD_WIDTH);
    y = WRAP_COORD(y, Config::WORLD_HEIGHT);
    return entityGrid[y][x];
}

// ============================================================================
// ⚖️ RuleSystem 实现 — 能量法则、变异法则、年龄法则、战斗法则
// ============================================================================
int32_t RuleSystem::calculateDecay(int32_t energy, int32_t age) {
    // 年龄越大，衰减越快
    double ageFactor = 1.0 + (static_cast<double>(age) / Config::MAX_AGE) * 2.0;
    return static_cast<int32_t>(Config::ENERGY_DECAY_RATE * ageFactor);
}

int32_t RuleSystem::calculateReplicationCost(int32_t parentEnergy) {
    // 复制代价与父体能量成比例 (确保复制后父体仍有能量)
    return Config::REPLICATION_COST + static_cast<int32_t>(parentEnergy * 0.2);
}

int32_t RuleSystem::calculateFightResult(int32_t attackerEnergy,
                                          int32_t defenderEnergy) {
    // 能量多者优势更大
    if (attackerEnergy > defenderEnergy) {
        return static_cast<int32_t>(defenderEnergy * 0.3);
    } else {
        return static_cast<int32_t>(defenderEnergy * 0.1);
    }
}

int32_t RuleSystem::calculateEnergyGainFromFood(int32_t foodAmount) {
    return std::min(foodAmount, Config::FOOD_ENERGY_PER_BITE);
}

double RuleSystem::effectiveMutationRate(int32_t generation, int32_t energy) {
    // 能量越低，变异率越高 (压力诱导变异)
    // 代数越高，变异率略微增加
    double baseRate = Config::MUTATION_RATE;
    double energyFactor = 1.0 + (1.0 - CLAMP(static_cast<double>(energy) / Config::MAX_ENERGY, 0.0, 1.0)) * 3.0;
    double genFactor = 1.0 + std::log(1.0 + generation) * 0.5;
    return baseRate * energyFactor * genFactor;
}

void RuleSystem::applyMutations(std::vector<uint8_t>& genome, std::mt19937& rng,
                                 int32_t generation, int32_t energy) {
    double mutRate = effectiveMutationRate(generation, energy);
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int32_t> byteDist(0, 255);

    // 点突变 - 随机翻转位
    for (size_t i = 0; i < genome.size(); ++i) {
        if (prob(rng) < mutRate) {
            // 翻转随机位
            int bit = std::uniform_int_distribution<int>(0, 7)(rng);
            genome[i] ^= (1 << bit);
        }
    }

    // 插入突变
    if (prob(rng) < Config::INSERTION_RATE && genome.size() < Config::PROGRAM_MEMORY_SIZE) {
        size_t pos = std::uniform_int_distribution<size_t>(0, genome.size())(rng);
        uint8_t newByte = static_cast<uint8_t>(byteDist(rng));
        genome.insert(genome.begin() + pos, newByte);
    }

    // 删除突变
    if (prob(rng) < Config::DELETION_RATE && genome.size() > 8) {
        size_t pos = std::uniform_int_distribution<size_t>(0, genome.size() - 1)(rng);
        genome.erase(genome.begin() + pos);
    }

    // 限制大小
    if (genome.size() > Config::PROGRAM_MEMORY_SIZE) {
        genome.resize(Config::PROGRAM_MEMORY_SIZE);
    }
}

bool RuleSystem::shouldDieOfAge(int32_t age, int32_t generation) {
    return age >= maxAgeForGeneration(generation);
}

int32_t RuleSystem::maxAgeForGeneration(int32_t generation) {
    return Config::MAX_AGE + generation * 50; // 后代寿命稍长
}

bool RuleSystem::fightResolve(int32_t attackerEnergy, int32_t defenderEnergy,
                               int32_t& energyTransfer, bool& attackerWins) {
    // 基于能量的概率战斗
    double attackerPower = static_cast<double>(attackerEnergy);
    double defenderPower = static_cast<double>(defenderEnergy);
    double totalPower = attackerPower + defenderPower;

    if (totalPower <= 0) {
        attackerWins = false;
        energyTransfer = 0;
        return false;
    }

    double attackerWinProb = attackerPower / totalPower;

    // 获胜者获取失败者能量的 30%
    if (attackerWinProb > 0.5) {
        attackerWins = true;
        energyTransfer = static_cast<int32_t>(defenderEnergy * 0.7);
    } else {
        attackerWins = false;
        energyTransfer = static_cast<int32_t>(defenderEnergy * 0.3);
    }
    return true;
}

bool RuleSystem::canReplicate(int32_t energy, int32_t age, int32_t generation,
                               int32_t nearbyCount) {
    return energy >= Config::REPLICATION_THRESHOLD
        && generation < Config::MAX_GENERATION
        && nearbyCount < 10; // 防止过度拥挤
}

bool RuleSystem::canFight(int32_t energy) {
    return energy >= Config::FIGHT_ENERGY_COST * 2;
}

bool RuleSystem::canMove(int32_t energy) {
    return energy >= Config::MOVE_ENERGY_COST;
}

bool RuleSystem::canSense(int32_t energy) {
    return energy >= Config::SENSE_ENERGY_COST;
}

// ============================================================================
// 🏗️ ECSManager 实现 — 实体 CRUD + 组件存取 + ID 回收
// ============================================================================
ECSManager::ECSManager()
    : m_entityCount(0), m_nextId(0)
{
    // 初始化内存池
    m_programPool = new MemoryPool(Config::PROGRAM_MEMORY_SIZE, Config::MAX_ENTITIES);
    m_dataPool    = new MemoryPool(Config::DATA_MEMORY_SIZE,    Config::MAX_ENTITIES);
    m_stackPool   = new MemoryPool(Config::STACK_SIZE * sizeof(int32_t), Config::MAX_ENTITIES);

    // 初始化组件
    for (int i = 0; i < Config::MAX_ENTITIES; ++i) {
        m_alive[i].alive = false;
        m_alive[i].deathTick = 0;
        m_ages[i].age = 0;
        m_ages[i].generation = 0;
        m_ages[i].parentId = -1;
        m_energies[i].energy = 0;
        m_energies[i].maxEnergy = Config::MAX_ENERGY;
        m_energies[i].energySpentThisTick = 0;
        m_positions[i].x = 0;
        m_positions[i].y = 0;
        m_positions[i].facing = 0;
    }
}

ECSManager::~ECSManager() {
    delete m_programPool;
    delete m_dataPool;
    delete m_stackPool;
}

int32_t ECSManager::createEntity() {
    int32_t id;
    if (!m_freeIds.empty()) {
        id = m_freeIds.back();
        m_freeIds.pop_back();
    } else {
        if (m_nextId >= Config::MAX_ENTITIES) return Entity::INVALID;
        id = m_nextId++;
    }

    m_entities[id] = Entity(id);
    m_entities[id].active = true;
    m_entities[id].componentMask = 0;

    // 默认添加存活组件
    m_alive[id] = AliveComponent();
    m_alive[id].alive = true;
    m_entities[id].addComponent(COMP_ALIVE);

    // 重置其他组件
    m_genomes[id] = GenomeComponent();
    m_energies[id] = EnergyComponent();
    m_energies[id].energy = Config::INITIAL_ENERGY;
    m_energies[id].maxEnergy = Config::MAX_ENERGY;
    m_positions[id] = PositionComponent();
    m_vmStates[id] = VMState();
    m_ages[id] = AgeComponent();
    m_sensors[id] = SensorComponent();

    ++m_entityCount;
    return id;
}

void ECSManager::destroyEntity(int32_t entityId) {
    if (entityId < 0 || entityId >= m_nextId) return;
    if (!m_entities[entityId].active) return;

    // 释放内存池
    if (m_vmStates[entityId].programMemoryBlock >= 0) {
        m_programPool->deallocate(m_vmStates[entityId].programMemoryBlock);
    }
    if (m_vmStates[entityId].dataMemoryBlock >= 0) {
        m_dataPool->deallocate(m_vmStates[entityId].dataMemoryBlock);
    }
    if (m_vmStates[entityId].stackMemoryBlock >= 0) {
        m_stackPool->deallocate(m_vmStates[entityId].stackMemoryBlock);
    }

    m_entities[entityId].active = false;
    m_entities[entityId].componentMask = 0;
    m_alive[entityId].alive = false;
    m_alive[entityId].deathTick = 0;
    m_freeIds.push_back(entityId);

    if (m_entityCount > 0) --m_entityCount;
}

bool ECSManager::isEntityAlive(int32_t entityId) const {
    if (entityId < 0 || entityId >= m_nextId) return false;
    return m_entities[entityId].active && m_alive[entityId].alive;
}

Entity& ECSManager::getEntity(int32_t entityId) {
    return m_entities[entityId];
}

const Entity& ECSManager::getEntity(int32_t entityId) const {
    return m_entities[entityId];
}

bool ECSManager::hasComponent(int32_t entityId, ComponentType type) const {
    if (entityId < 0 || entityId >= m_nextId) return false;
    return m_entities[entityId].hasComponent(type);
}

GenomeComponent& ECSManager::getGenome(int32_t entityId) {
    return m_genomes[entityId];
}

EnergyComponent& ECSManager::getEnergy(int32_t entityId) {
    return m_energies[entityId];
}

PositionComponent& ECSManager::getPosition(int32_t entityId) {
    return m_positions[entityId];
}

VMState& ECSManager::getVMState(int32_t entityId) {
    return m_vmStates[entityId];
}

AgeComponent& ECSManager::getAge(int32_t entityId) {
    return m_ages[entityId];
}

AliveComponent& ECSManager::getAlive(int32_t entityId) {
    return m_alive[entityId];
}

SensorComponent& ECSManager::getSensor(int32_t entityId) {
    return m_sensors[entityId];
}

std::vector<int32_t> ECSManager::getAliveEntityIds() const {
    std::vector<int32_t> result;
    for (int32_t i = 0; i < m_nextId; ++i) {
        if (m_entities[i].active && m_alive[i].alive) {
            result.push_back(i);
        }
    }
    return result;
}

int32_t ECSManager::getAliveCount() const {
    int32_t count = 0;
    for (int32_t i = 0; i < m_nextId; ++i) {
        if (m_entities[i].active && m_alive[i].alive) ++count;
    }
    return count;
}

// ============================================================================
// 1️⃣ MetabolismSystem 实现 — 能量衰减 + 能量耗尽死亡
// ============================================================================
void MetabolismSystem::execute(ECSManager& ecs, WorldState& world,
                                std::mt19937& rng) {
    (void)rng;
    auto aliveIds = ecs.getAliveEntityIds();

    for (int32_t id : aliveIds) {
        auto& energy = ecs.getEnergy(id);
        auto& age = ecs.getAge(id);

        int32_t decay = RuleSystem::calculateDecay(energy.energy, age.age);
        energy.spend(decay);

        // 能量耗尽则标记死亡
        if (energy.isDead()) {
            auto& alive = ecs.getAlive(id);
            alive.alive = false;
            alive.deathTick = static_cast<int32_t>(world.tickCount);
            alive.deathCause = "energy_depleted";
            world.totalDeaths++;

            // 实体死亡时，将其能量转化为食物洒在原地
            auto& pos = ecs.getPosition(id);
            world.addFood(pos.x, pos.y, std::max(0, energy.energy + decay));
            world.clearEntity(pos.x, pos.y);
        }
    }
}

// ============================================================================
// 2️⃣ SenseSystem 实现 — 填充传感器组件
// ============================================================================
void SenseSystem::execute(ECSManager& ecs, WorldState& world,
                           std::mt19937& rng) {
    (void)rng;
    auto aliveIds = ecs.getAliveEntityIds();

    for (int32_t id : aliveIds) {
        auto& sensor = ecs.getSensor(id);
        auto& pos = ecs.getPosition(id);

        // 重置传感器
        sensor.nearestEntityId = -1;
        sensor.nearestDistance = Config::SENSE_RANGE + 1;
        sensor.nearestEnergy = 0;
        sensor.nearestDirection = 0;
        sensor.entityCountNearby = 0;
        sensor.avgEnergyNearby = 0;

        // 感知当前位置食物
        sensor.foodAtPosition = world.getFood(pos.x, pos.y);

        // 感知附近实体
        int32_t totalEnergy = 0;
        for (int32_t otherId : aliveIds) {
            if (otherId == id) continue;
            auto& otherPos = ecs.getPosition(otherId);
            int32_t dist = pos.distanceTo(otherPos);
            if (dist <= Config::SENSE_RANGE) {
                sensor.entityCountNearby++;
                totalEnergy += ecs.getEnergy(otherId).energy;
                if (dist < sensor.nearestDistance) {
                    sensor.nearestDistance = dist;
                    sensor.nearestEntityId = otherId;
                    sensor.nearestEnergy = ecs.getEnergy(otherId).energy;
                    sensor.nearestDirection = pos.directionTo(otherPos);
                }
            }
        }
        if (sensor.entityCountNearby > 0) {
            sensor.avgEnergyNearby = totalEnergy / sensor.entityCountNearby;
        }
    }
}

// ============================================================================
// 3️⃣ VMExecutionSystem 实现 — 取指、解码、执行 35 条指令
// ============================================================================
uint8_t VMExecutionSystem::fetchOpcode(VMState& vm, int32_t entityId,
                                        ECSManager& ecs) {
    if (vm.ip >= static_cast<uint16_t>(vm.genome.size())) {
        return ISA::HALT; // 超出程序范围
    }
    return vm.genome[vm.ip];
}

int32_t VMExecutionSystem::fetchImmediate(VMState& vm, int32_t entityId,
                                           ECSManager& ecs) {
    uint16_t nextIp = vm.ip;  // vm.ip 已指向立即数字节
    if (nextIp >= static_cast<uint16_t>(vm.genome.size())) {
        return 0;
    }
    // 读取1-2字节立即数
    int32_t val = static_cast<int32_t>(vm.genome[nextIp]);
    vm.ip = nextIp;
    // 如果高位为1，继续读下一字节
    if (val & 0x80) {
        val &= 0x7F;
        nextIp = vm.ip + 1;
        if (nextIp < static_cast<uint16_t>(vm.genome.size())) {
            val = (val << 7) | static_cast<int32_t>(vm.genome[nextIp] & 0x7F);
            vm.ip = nextIp;
        }
    }
    return val;
}

// 执行单条指令 — 整个模拟最核心的函数
// 返回 true=继续执行, false=停机或实体死亡
bool VMExecutionSystem::executeInstruction(ECSManager& ecs, WorldState& world,
                                            int32_t entityId, std::mt19937& rng) {
    auto& vm = ecs.getVMState(entityId);
    auto& sensor = ecs.getSensor(entityId);
    auto& energy = ecs.getEnergy(entityId);
    auto& pos = ecs.getPosition(entityId);
    auto& age = ecs.getAge(entityId);

    if (vm.halted) return false;
    if (vm.ip >= static_cast<uint16_t>(vm.genome.size())) {
        vm.halted = true;
        return false;
    }

    uint8_t opcode = fetchOpcode(vm, entityId, ecs);
    vm.ip++;

    // 辅助: 读取寄存器编号 (后续字节的低3位)
    auto readReg = [&]() -> uint8_t {
        uint16_t nextIp = vm.ip;
        if (nextIp >= static_cast<uint16_t>(vm.genome.size())) return 0;
        uint8_t val = vm.genome[nextIp];
        vm.ip = nextIp + 1;
        return val & 0x07;
    };

    // 辅助: 读取立即数
    auto readImm = [&]() -> int32_t {
        return fetchImmediate(vm, entityId, ecs);
    };

    switch (opcode) {
        // ====== NOP ======
        case ISA::NOP:
            break;

        // ====== MOV REG, IMM/REG ======
        case ISA::MOV: {
            uint8_t rd = readReg();
            // 直接读取模式字节 (不用readReg，因为需要完整8位)
            uint16_t nextIp = vm.ip;
            uint8_t mode = (nextIp < static_cast<uint16_t>(vm.genome.size()))
                           ? vm.genome[nextIp] : 0;
            vm.ip = nextIp + 1;
            if (mode & 0x10) {
                // MOV REG, IMM
                int32_t imm = readImm();
                if (rd < Config::NUM_REGISTERS) vm.regs[rd] = imm;
            } else {
                // MOV REG, REG
                uint8_t rs = mode & 0x07;
                if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS)
                    vm.regs[rd] = vm.regs[rs];
            }
            break;
        }

        // ====== ADD REG, REG ======
        case ISA::ADD: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS) {
                vm.regs[rd] += vm.regs[rs];
            }
            break;
        }

        // ====== SUB REG, REG ======
        case ISA::SUB: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS) {
                vm.regs[rd] -= vm.regs[rs];
            }
            break;
        }

        // ====== MUL REG, REG (结果低32位) ======
        case ISA::MUL: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS) {
                vm.regs[rd] *= vm.regs[rs];
            }
            break;
        }

        // ====== DIV REG, REG ======
        case ISA::DIV: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS) {
                if (vm.regs[rs] != 0) {
                    vm.regs[rd] /= vm.regs[rs];
                }
            }
            break;
        }

        // ====== INC REG ======
        case ISA::INC: {
            uint8_t rd = readReg();
            if (rd < Config::NUM_REGISTERS) vm.regs[rd]++;
            break;
        }

        // ====== DEC REG ======
        case ISA::DEC: {
            uint8_t rd = readReg();
            if (rd < Config::NUM_REGISTERS) vm.regs[rd]--;
            break;
        }

        // ====== AND REG, REG ======
        case ISA::AND_OP: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS)
                vm.regs[rd] &= vm.regs[rs];
            break;
        }

        // ====== OR REG, REG ======
        case ISA::OR_OP: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS)
                vm.regs[rd] |= vm.regs[rs];
            break;
        }

        // ====== XOR REG, REG ======
        case ISA::XOR_OP: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS)
                vm.regs[rd] ^= vm.regs[rs];
            break;
        }

        // ====== NOT REG ======
        case ISA::NOT_OP: {
            uint8_t rd = readReg();
            if (rd < Config::NUM_REGISTERS) vm.regs[rd] = ~vm.regs[rd];
            break;
        }

        // ====== SHL REG, IMM ======
        case ISA::SHL: {
            uint8_t rd = readReg();
            int32_t imm = readImm() & 0x1F;
            if (rd < Config::NUM_REGISTERS) vm.regs[rd] <<= imm;
            break;
        }

        // ====== SHR REG, IMM ======
        case ISA::SHR: {
            uint8_t rd = readReg();
            int32_t imm = readImm() & 0x1F;
            if (rd < Config::NUM_REGISTERS) vm.regs[rd] >>= imm;
            break;
        }

        // ====== CMP REG, REG ======
        case ISA::CMP: {
            uint8_t rd = readReg();
            uint8_t rs = readReg();
            if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS) {
                int32_t result = vm.regs[rd] - vm.regs[rs];
                vm.flags = 0;
                if (result == 0) vm.flags |= ISA::FLAG_ZERO;
                if (result < 0)  vm.flags |= ISA::FLAG_NEGATIVE;
            }
            break;
        }

        // ====== JMP ADDR ======
        case ISA::JMP: {
            int32_t addr = readImm();
            vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            break;
        }

        // ====== JE ADDR ======
        case ISA::JE: {
            int32_t addr = readImm();
            if (vm.flags & ISA::FLAG_ZERO) {
                vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            }
            break;
        }

        // ====== JNE ADDR ======
        case ISA::JNE: {
            int32_t addr = readImm();
            if (!(vm.flags & ISA::FLAG_ZERO)) {
                vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            }
            break;
        }

        // ====== JG ADDR ======
        case ISA::JG: {
            int32_t addr = readImm();
            if (!(vm.flags & ISA::FLAG_ZERO) && !(vm.flags & ISA::FLAG_NEGATIVE)) {
                vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            }
            break;
        }

        // ====== JL ADDR ======
        case ISA::JL: {
            int32_t addr = readImm();
            if (vm.flags & ISA::FLAG_NEGATIVE) {
                vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            }
            break;
        }

        // ====== JGE ADDR ======
        case ISA::JGE: {
            int32_t addr = readImm();
            if (!(vm.flags & ISA::FLAG_NEGATIVE)) {
                vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            }
            break;
        }

        // ====== JLE ADDR ======
        case ISA::JLE: {
            int32_t addr = readImm();
            if ((vm.flags & ISA::FLAG_ZERO) || (vm.flags & ISA::FLAG_NEGATIVE)) {
                vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            }
            break;
        }

        // ====== PUSH REG ======
        case ISA::PUSH: {
            uint8_t rs = readReg();
            if (rs < Config::NUM_REGISTERS && vm.sp < Config::STACK_SIZE) {
                uint8_t* stack = ecs.getStackMemory().getBlock(vm.stackMemoryBlock);
                if (stack) {
                    int32_t* sp = reinterpret_cast<int32_t*>(stack);
                    sp[vm.sp] = vm.regs[rs];
                    vm.sp++;
                }
            }
            break;
        }

        // ====== POP REG ======
        case ISA::POP: {
            uint8_t rd = readReg();
            if (rd < Config::NUM_REGISTERS && vm.sp > 0) {
                vm.sp--;
                uint8_t* stack = ecs.getStackMemory().getBlock(vm.stackMemoryBlock);
                if (stack) {
                    int32_t* sp = reinterpret_cast<int32_t*>(stack);
                    vm.regs[rd] = sp[vm.sp];
                }
            }
            break;
        }

        // ====== CALL ADDR ======
        case ISA::CALL: {
            int32_t addr = readImm();
            // 将返回地址压栈
            if (vm.sp < Config::STACK_SIZE) {
                uint8_t* stack = ecs.getStackMemory().getBlock(vm.stackMemoryBlock);
                if (stack) {
                    int32_t* sp = reinterpret_cast<int32_t*>(stack);
                    sp[vm.sp] = static_cast<int32_t>(vm.ip);
                    vm.sp++;
                }
            }
            vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE);
            break;
        }

        // ====== RET ======
        case ISA::RET: {
            if (vm.sp > 0) {
                vm.sp--;
                uint8_t* stack = ecs.getStackMemory().getBlock(vm.stackMemoryBlock);
                if (stack) {
                    int32_t* sp = reinterpret_cast<int32_t*>(stack);
                    vm.ip = static_cast<uint16_t>(sp[vm.sp] % Config::PROGRAM_MEMORY_SIZE);
                }
            }
            break;
        }

        // ====== LOAD REG, ADDR (从数据内存加载) ======
        case ISA::LOAD: {
            uint8_t rd = readReg();
            int32_t addr = readImm() % Config::DATA_MEMORY_SIZE;
            if (rd < Config::NUM_REGISTERS) {
                uint8_t* data = ecs.getDataMemory().getBlock(vm.dataMemoryBlock);
                if (data) {
                    int32_t* mem = reinterpret_cast<int32_t*>(data);
                    vm.regs[rd] = mem[addr % (Config::DATA_MEMORY_SIZE / sizeof(int32_t))];
                }
            }
            break;
        }

        // ====== STORE ADDR, REG (存入数据内存) ======
        case ISA::STORE: {
            int32_t addr = readImm() % (Config::DATA_MEMORY_SIZE / sizeof(int32_t));
            uint8_t rs = readReg();
            if (rs < Config::NUM_REGISTERS) {
                uint8_t* data = ecs.getDataMemory().getBlock(vm.dataMemoryBlock);
                if (data) {
                    int32_t* mem = reinterpret_cast<int32_t*>(data);
                    mem[addr] = vm.regs[rs];
                }
            }
            break;
        }

        // ====== 自我复制 ======
        case ISA::REPLICATE: {
            energy.spend(Config::REPLICATION_COST / 1.2); // 立即消耗部分能量
            int32_t childId = ReplicationSystem::replicateEntity(ecs, world, entityId, rng);
            vm.regs[0] = childId; // R0 = 子实体ID (-1 失败)
            world.totalReplications++;
            break;
        }

        // ====== 进食 ======
        case ISA::EAT: {
            bool success = EatingSystem::eatAtPosition(ecs, world, entityId);
            vm.regs[0] = success ? 1 : 0;
            break;
        }

        // ====== 战斗 ======
        case ISA::FIGHT: {
            // R1 = 目标方向
            int32_t direction = CLAMP(vm.regs[1], 0, 3);
            int32_t tx = pos.x, ty = pos.y;
            switch (direction) {
                case 0: ty = WRAP_COORD(ty - 1, Config::WORLD_HEIGHT); break;
                case 1: tx = WRAP_COORD(tx + 1, Config::WORLD_WIDTH);  break;
                case 2: ty = WRAP_COORD(ty + 1, Config::WORLD_HEIGHT); break;
                case 3: tx = WRAP_COORD(tx - 1, Config::WORLD_WIDTH);  break;
            }
            int32_t targetId = world.getEntity(tx, ty);
            bool success = false;
            if (targetId >= 0 && targetId != entityId) {
                success = CombatSystem::fightEntity(ecs, world, entityId, targetId);
            }
            vm.regs[0] = success ? 1 : 0;
            world.totalFights++;
            break;
        }

        // ====== 感知环境 ======
        case ISA::SENSE: {
            energy.spend(Config::SENSE_ENERGY_COST);
            // 将传感器数据加载到寄存器
            vm.regs[0] = sensor.nearestEntityId;   // R0 = 最近实体ID
            vm.regs[1] = sensor.nearestDistance;    // R1 = 最近实体距离
            vm.regs[2] = sensor.nearestEnergy;      // R2 = 最近实体能量
            vm.regs[3] = sensor.nearestDirection;   // R3 = 最近实体方向
            vm.regs[4] = sensor.foodAtPosition;     // R4 = 当前位置食物
            vm.regs[5] = sensor.entityCountNearby;  // R5 = 附近实体数量
            vm.regs[6] = energy.energy;             // R6 = 自身能量
            vm.regs[7] = age.age;                   // R7 = 自身年龄
            break;
        }

        // ====== 移动 ======
        case ISA::MOVE: {
            int32_t direction = CLAMP(vm.regs[0], 0, 3);
            bool success = MovementSystem::moveEntity(ecs, world, entityId, direction);
            vm.regs[0] = success ? 1 : 0;
            break;
        }

        // ====== 自我终结 ======
        case ISA::DIE: {
            auto& alive = ecs.getAlive(entityId);
            alive.alive = false;
            alive.deathTick = static_cast<int32_t>(world.tickCount);
            alive.deathCause = "self_termination";
            vm.halted = true;
            world.clearEntity(pos.x, pos.y);
            // 将剩余能量转化为食物
            world.addFood(pos.x, pos.y, energy.energy);
            energy.energy = 0;
            world.totalDeaths++;
            return false;
        }

        // ====== HALT ======
        case ISA::HALT:
            vm.halted = true;
            return false;

        default:
            // 未知指令 - 当作 NOP
            break;
    }

    // 限制 IP 范围
    if (vm.ip >= Config::PROGRAM_MEMORY_SIZE) {
        vm.ip = 0;
    }

    return !vm.halted;
}

void VMExecutionSystem::execute(ECSManager& ecs, WorldState& world,
                                 std::mt19937& rng) {
    auto aliveIds = ecs.getAliveEntityIds();

    for (int32_t id : aliveIds) {
        auto& vm = ecs.getVMState(id);
        auto& energy = ecs.getEnergy(id);

        energy.energySpentThisTick = 0;

        // 执行 N 条指令
        for (int i = 0; i < Config::TICKS_PER_STEP; ++i) {
            if (vm.halted) break;
            if (energy.isDead()) {
                vm.halted = true;
                break;
            }
            if (!executeInstruction(ecs, world, id, rng)) {
                break;
            }
        }
    }
}

// ============================================================================
// 4️⃣ MovementSystem 实现 — 实体移动 + 网格重建
// ============================================================================
bool MovementSystem::moveEntity(ECSManager& ecs, WorldState& world,
                                 int32_t entityId, int32_t direction) {
    auto& energy = ecs.getEnergy(entityId);
    if (!RuleSystem::canMove(energy.energy)) return false;

    auto& pos = ecs.getPosition(entityId);
    int32_t oldX = pos.x, oldY = pos.y;

    pos.move(direction);
    int32_t newX = pos.x, newY = pos.y;

    // 检查目标位置是否被占用
    int32_t occupant = world.getEntity(newX, newY);
    if (occupant >= 0 && occupant != entityId) {
        // 已占用，移回原位
        pos.x = oldX;
        pos.y = oldY;
        return false;
    }

    // 更新世界网格
    world.clearEntity(oldX, oldY);
    world.setEntity(newX, newY, entityId);

    energy.spend(Config::MOVE_ENERGY_COST);
    return true;
}

void MovementSystem::execute(ECSManager& ecs, WorldState& world,
                              std::mt19937& rng) {
    (void)rng;
    // 移动由 VM 的 MOVE 指令驱动，这里确保网格一致性
    // 重建网格
    for (int y = 0; y < Config::WORLD_HEIGHT; ++y)
        for (int x = 0; x < Config::WORLD_WIDTH; ++x)
            world.entityGrid[y][x] = -1;

    auto aliveIds = ecs.getAliveEntityIds();
    for (int32_t id : aliveIds) {
        auto& pos = ecs.getPosition(id);
        // 如果该位置已被占用，存入能量食物并重新分配
        if (world.entityGrid[pos.y][pos.x] >= 0) {
            // 冲突！随机分配
            int32_t newX = std::uniform_int_distribution<int32_t>(0, Config::WORLD_WIDTH - 1)(rng);
            int32_t newY = std::uniform_int_distribution<int32_t>(0, Config::WORLD_HEIGHT - 1)(rng);
            pos.x = newX;
            pos.y = newY;
        }
        world.entityGrid[pos.y][pos.x] = id;
    }
}

// ============================================================================
// 5️⃣ EatingSystem 实现 — 消耗食物获取能量
// ============================================================================
bool EatingSystem::eatAtPosition(ECSManager& ecs, WorldState& world,
                                  int32_t entityId) {
    auto& energy = ecs.getEnergy(entityId);
    auto& pos = ecs.getPosition(entityId);

    int32_t foodAmount = world.getFood(pos.x, pos.y);
    if (foodAmount <= 0) return false;

    int32_t gained = RuleSystem::calculateEnergyGainFromFood(foodAmount);
    world.consumeFood(pos.x, pos.y, gained);
    energy.gain(gained);
    return true;
}

void EatingSystem::execute(ECSManager& ecs, WorldState& world,
                            std::mt19937& rng) {
    (void)rng;
    // 进食主要由 VM 的 EAT 指令驱动
    // 这里不做额外处理
    (void)ecs;
    (void)world;
}

// ============================================================================
// 6️⃣ CombatSystem 实现 — 能量争夺战斗
// ============================================================================
bool CombatSystem::fightEntity(ECSManager& ecs, WorldState& world,
                                int32_t attackerId, int32_t targetId) {
    if (!ecs.isEntityAlive(targetId)) return false;

    auto& attackerEnergy = ecs.getEnergy(attackerId);
    auto& targetEnergy = ecs.getEnergy(targetId);

    if (!RuleSystem::canFight(attackerEnergy.energy)) return false;

    // 消耗战斗能量
    attackerEnergy.spend(Config::FIGHT_ENERGY_COST);

    int32_t energyTransfer;
    bool attackerWins;
    RuleSystem::fightResolve(attackerEnergy.energy, targetEnergy.energy,
                              energyTransfer, attackerWins);

    if (attackerWins) {
        targetEnergy.spend(energyTransfer);
        attackerEnergy.gain(energyTransfer);
    } else {
        attackerEnergy.spend(energyTransfer / 2);
    }

    // 检查目标是否死亡
    if (targetEnergy.isDead()) {
        auto& targetAlive = ecs.getAlive(targetId);
        targetAlive.alive = false;
        targetAlive.deathTick = static_cast<int32_t>(world.tickCount);
        targetAlive.deathCause = "killed_in_combat";
        auto& targetPos = ecs.getPosition(targetId);
        world.clearEntity(targetPos.x, targetPos.y);
        world.addFood(targetPos.x, targetPos.y, std::max(0, targetEnergy.energy));
        targetEnergy.energy = 0;
        world.totalDeaths++;
    }

    return true;
}

void CombatSystem::execute(ECSManager& ecs, WorldState& world,
                            std::mt19937& rng) {
    (void)rng;
    // 战斗由 VM 的 FIGHT 指令驱动
    (void)ecs;
    (void)world;
}

// ============================================================================
// 7️⃣ ReplicationSystem 实现 — 自我复制 + 基因变异
// ============================================================================
int32_t ReplicationSystem::replicateEntity(ECSManager& ecs, WorldState& world,
                                            int32_t parentId, std::mt19937& rng) {
    auto& parentEnergy = ecs.getEnergy(parentId);
    auto& parentAge = ecs.getAge(parentId);
    auto& parentPos = ecs.getPosition(parentId);

    // 检查复制条件
    auto& sensor = ecs.getSensor(parentId);
    if (!RuleSystem::canReplicate(parentEnergy.energy, parentAge.age,
                                   parentAge.generation, sensor.entityCountNearby)) {
        return -1;
    }

    // 计算复制消耗
    int32_t cost = RuleSystem::calculateReplicationCost(parentEnergy.energy);
    if (parentEnergy.energy < cost) return -1;
    parentEnergy.spend(cost);

    // 创建子实体
    int32_t childId = ecs.createEntity();
    if (childId < 0) {
        // 创建失败，退还能量
        parentEnergy.gain(cost);
        return -1;
    }

    // 分配内存池
    auto& childVM = ecs.getVMState(childId);
    childVM.programMemoryBlock = ecs.getProgramMemory().allocate();
    childVM.dataMemoryBlock = ecs.getDataMemory().allocate();
    childVM.stackMemoryBlock = ecs.getStackMemory().allocate();

    // 复制基因组
    auto& parentGenome = ecs.getGenome(parentId);
    auto& childGenome = ecs.getGenome(childId);
    childGenome.code = parentGenome.code; // 复制基因

    // 应用变异
    bool mutated = false;
    std::vector<uint8_t> mutatedGenome = childGenome.code;
    RuleSystem::applyMutations(mutatedGenome, rng, parentAge.generation + 1,
                                parentEnergy.energy);
    if (mutatedGenome != childGenome.code) {
        mutated = true;
        world.totalMutations++;
    }
    childGenome.code = mutatedGenome;
    childGenome.parentChecksum = parentGenome.computeChecksum();
    childGenome.checksum = childGenome.computeChecksum();

    // 加载基因组到 VM
    childVM.loadGenome(childGenome.code);

    // 复制部分数据内存
    auto& parentVM = ecs.getVMState(parentId);
    if (parentVM.dataMemoryBlock >= 0 && childVM.dataMemoryBlock >= 0) {
        uint8_t* parentData = ecs.getDataMemory().getBlock(parentVM.dataMemoryBlock);
        uint8_t* childData = ecs.getDataMemory().getBlock(childVM.dataMemoryBlock);
        if (parentData && childData) {
            std::memcpy(childData, parentData, Config::DATA_MEMORY_SIZE);
        }
    }

    // 设置子实体属性
    auto& childEnergy = ecs.getEnergy(childId);
    childEnergy.energy = Config::INITIAL_ENERGY / 2;
    childEnergy.maxEnergy = Config::MAX_ENERGY;

    // 位置：在父体附近
    int32_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    auto& childPos = ecs.getPosition(childId);
    bool placed = false;
    for (int i = 0; i < 4; ++i) {
        int32_t nx = WRAP_COORD(parentPos.x + offsets[i][0], Config::WORLD_WIDTH);
        int32_t ny = WRAP_COORD(parentPos.y + offsets[i][1], Config::WORLD_HEIGHT);
        if (world.getEntity(nx, ny) < 0) {
            childPos.x = nx;
            childPos.y = ny;
            world.setEntity(nx, ny, childId);
            placed = true;
            break;
        }
    }
    if (!placed) {
        // 随机位置
        std::uniform_int_distribution<int32_t> xDist(0, Config::WORLD_WIDTH - 1);
        std::uniform_int_distribution<int32_t> yDist(0, Config::WORLD_HEIGHT - 1);
        childPos.x = xDist(rng);
        childPos.y = yDist(rng);
        world.setEntity(childPos.x, childPos.y, childId);
    }

    // 设置年龄和代数
    auto& childAge = ecs.getAge(childId);
    childAge.age = 0;
    childAge.generation = parentAge.generation + 1;
    childAge.parentId = parentId;

    // 标记组件
    auto& childEntity = ecs.getEntity(childId);
    childEntity.addComponent(COMP_GENOME);
    childEntity.addComponent(COMP_ENERGY);
    childEntity.addComponent(COMP_POSITION);
    childEntity.addComponent(COMP_VMSTATE);
    childEntity.addComponent(COMP_AGE);
    childEntity.addComponent(COMP_SENSOR);

    world.totalBirths++;
    return childId;
}

void ReplicationSystem::execute(ECSManager& ecs, WorldState& world,
                                 std::mt19937& rng) {
    (void)rng;
    // 复制主要由 VM 的 REPLICATE 指令驱动
    (void)ecs;
    (void)world;
}

// ============================================================================
// 8️⃣ DeathSystem 实现 — 老死判定 + 能量耗尽 + 实体销毁
// ============================================================================
void DeathSystem::execute(ECSManager& ecs, WorldState& world,
                           std::mt19937& rng) {
    (void)rng;
    auto aliveIds = ecs.getAliveEntityIds();

    for (int32_t id : aliveIds) {
        auto& alive = ecs.getAlive(id);
        auto& age = ecs.getAge(id);
        auto& energy = ecs.getEnergy(id);
        auto& pos = ecs.getPosition(id);

        // 老死
        if (RuleSystem::shouldDieOfAge(age.age, age.generation)) {
            alive.alive = false;
            alive.deathTick = static_cast<int32_t>(world.tickCount);
            alive.deathCause = "old_age";
            world.clearEntity(pos.x, pos.y);
            world.addFood(pos.x, pos.y, std::max(0, energy.energy));
            energy.energy = 0;
            world.totalDeaths++;
            continue;
        }

        // 能量耗尽
        if (energy.isDead() && alive.alive) {
            alive.alive = false;
            alive.deathTick = static_cast<int32_t>(world.tickCount);
            alive.deathCause = "energy_depleted";
            world.clearEntity(pos.x, pos.y);
            world.totalDeaths++;
            continue;
        }
    }

    // 清理死亡实体
    for (int32_t id = 0; id < Config::MAX_ENTITIES; ++id) {
        if (ecs.getEntity(id).active && !ecs.getAlive(id).alive) {
            ecs.destroyEntity(id);
        }
    }
}

// ============================================================================
// 🎮 DigitalLifeSimulator 实现 — 顶层协调器
// ============================================================================
DigitalLifeSimulator::DigitalLifeSimulator()
    : m_rng(static_cast<uint32_t>(std::chrono::steady_clock::now()
                                   .time_since_epoch().count()))
{
    setupSystems();
}

void DigitalLifeSimulator::setupSystems() {
    m_systems.push_back(new MetabolismSystem());
    m_systems.push_back(new SenseSystem());
    m_systems.push_back(new VMExecutionSystem());
    m_systems.push_back(new MovementSystem());
    m_systems.push_back(new EatingSystem());
    m_systems.push_back(new CombatSystem());
    m_systems.push_back(new ReplicationSystem());
    m_systems.push_back(new DeathSystem());
}

void DigitalLifeSimulator::initialize(std::mt19937& rng) {
    m_rng = rng;
    m_world.reset();

    // 生成初始食物
    for (int i = 0; i < 100; ++i) {
        m_world.spawnFood(m_rng);
    }

    // 生成原始生命
    int32_t centerX = Config::WORLD_WIDTH / 2;
    int32_t centerY = Config::WORLD_HEIGHT / 2;

    // 生成几种不同的种子基因组
    for (int i = 0; i < 8; ++i) {
        auto genome = generateSeedGenome(m_rng);
        // 添加一些变异使每个种子不同
        RuleSystem::applyMutations(genome, m_rng, 0, 100);

        int32_t x = centerX + (i % 3) - 1;
        int32_t y = centerY + (i / 3) - 1;
        spawnPrimordialLife(m_rng, x, y, genome);
    }
}

int32_t DigitalLifeSimulator::spawnPrimordialLife(
    std::mt19937& rng, int32_t x, int32_t y,
    const std::vector<uint8_t>& genome) {

    int32_t entityId = m_ecs.createEntity();
    if (entityId < 0) return -1;

    // 分配内存
    auto& vm = m_ecs.getVMState(entityId);
    vm.programMemoryBlock = m_ecs.getProgramMemory().allocate();
    vm.dataMemoryBlock = m_ecs.getDataMemory().allocate();
    vm.stackMemoryBlock = m_ecs.getStackMemory().allocate();

    // 设置基因组
    auto& genomeComp = m_ecs.getGenome(entityId);
    genomeComp.code = genome;
    genomeComp.checksum = genomeComp.computeChecksum();
    genomeComp.parentChecksum = 0;

    vm.loadGenome(genome);

    // 设置能量
    auto& energy = m_ecs.getEnergy(entityId);
    energy.energy = Config::INITIAL_ENERGY;
    energy.maxEnergy = Config::MAX_ENERGY;

    // 设置位置
    auto& pos = m_ecs.getPosition(entityId);
    pos.x = WRAP_COORD(x, Config::WORLD_WIDTH);
    pos.y = WRAP_COORD(y, Config::WORLD_HEIGHT);

    // 找空位
    if (m_world.getEntity(pos.x, pos.y) >= 0) {
        std::uniform_int_distribution<int32_t> xDist(0, Config::WORLD_WIDTH - 1);
        std::uniform_int_distribution<int32_t> yDist(0, Config::WORLD_HEIGHT - 1);
        pos.x = xDist(rng);
        pos.y = yDist(rng);
    }
    m_world.setEntity(pos.x, pos.y, entityId);

    // 设置年龄
    auto& age = m_ecs.getAge(entityId);
    age.age = 0;
    age.generation = 0;
    age.parentId = -1;

    // 标记组件
    auto& entity = m_ecs.getEntity(entityId);
    entity.addComponent(COMP_GENOME);
    entity.addComponent(COMP_ENERGY);
    entity.addComponent(COMP_POSITION);
    entity.addComponent(COMP_VMSTATE);
    entity.addComponent(COMP_AGE);
    entity.addComponent(COMP_SENSOR);

    return entityId;
}

void DigitalLifeSimulator::tick() {
    m_world.tickCount++;

    // 生成食物
    m_world.spawnFood(m_rng);

    // 年龄增长
    m_ecs.forEachAlive([&](int32_t id, Entity& e) {
        auto& age = m_ecs.getAge(id);
        age.age++;
    });

    // 按顺序执行所有系统
    for (auto* sys : m_systems) {
        sys->execute(m_ecs, m_world, m_rng);
    }
}

void DigitalLifeSimulator::run(int32_t numTicks, bool verbose) {
    for (int32_t i = 0; i < numTicks; ++i) {
        tick();
        if (verbose && (i % 10 == 0)) {
            std::cout << "Tick " << m_world.tickCount
                      << " | Alive: " << m_ecs.getAliveCount()
                      << " | Births: " << m_world.totalBirths
                      << " | Deaths: " << m_world.totalDeaths
                      << " | Mutations: " << m_world.totalMutations
                      << std::endl;
        }
    }
}

// ============================================================================
// 基因组生成
// ============================================================================

// 随机基因组
std::vector<uint8_t> DigitalLifeSimulator::generateRandomGenome(
    std::mt19937& rng, size_t length) {
    std::vector<uint8_t> genome(length);
    std::uniform_int_distribution<int32_t> byteDist(0, 255);
    for (size_t i = 0; i < length; ++i) {
        genome[i] = static_cast<uint8_t>(byteDist(rng));
    }
    return genome;
}

// 种子基因组 - 包含基本生存行为的程序
std::vector<uint8_t> DigitalLifeSimulator::generateSeedGenome(
    std::mt19937& rng) {

    std::vector<uint8_t> genome;

    // 设计一个能在培养皿中生存的基本程序：
    // 策略：SENSE -> 判断 -> 行动
    //
    // 伪代码:
    // loop:
    //   SENSE           ; 感知环境
    //   CMP R4, 10      ; 比较食物量
    //   JG  eat_label    ; 如果食物 > 10，去吃
    //   CMP R1, 3        ; 比较最近实体距离
    //   JL  fight_label  ; 如果很近，战斗
    //   CMP R6, 80       ; 比较自身能量
    //   JG  replicate_label ; 能量高就复制
    //   MOV R0, R3       ; 向目标移动
    //   MOVE
    //   JMP loop
    // eat_label:
    //   EAT
    //   JMP loop
    // fight_label:
    //   MOV R1, R3       ; 朝向目标
    //   FIGHT
    //   JMP loop
    // replicate_label:
    //   REPLICATE
    //   JMP loop

    auto addMovImm = [&](uint8_t rd, int32_t imm) {
        genome.push_back(ISA::MOV);
        genome.push_back(rd & 0x07);
        genome.push_back(0x10); // MOV REG, IMM 模式标记
        // 简单立即数编码
        if (imm < 128) {
            genome.push_back(static_cast<uint8_t>(imm));
        } else {
            genome.push_back(static_cast<uint8_t>(0x80 | (imm & 0x7F)));
            genome.push_back(static_cast<uint8_t>((imm >> 7) & 0x7F));
        }
    };

    auto addMovReg = [&](uint8_t rd, uint8_t rs) {
        genome.push_back(ISA::MOV);
        genome.push_back(rd & 0x07);
        genome.push_back(rs & 0x07); // mode = 0 (reg-reg)
    };

    auto addCmp = [&](uint8_t rd, uint8_t rs) {
        genome.push_back(ISA::CMP);
        genome.push_back(rd & 0x07);
        genome.push_back(rs & 0x07);
    };

    auto addJmp = [&](ISA::Opcode jmpType, uint8_t addr) {
        genome.push_back(jmpType);
        genome.push_back(addr);
    };

    auto addLabel = [&](uint8_t addr) { /* 不占空间 */ };

    // === 种子程序 ===
    uint8_t loop_start = static_cast<uint8_t>(genome.size());

    // SENSE
    genome.push_back(ISA::SENSE);

    // CMP R4, ? ; R4 = foodAtPosition
    // 我们比较 R4 与某个值，但 CMP 只能比较寄存器
    // 先用 MOV 把阈值放入 R5（使用 R0 临时比较）
    // 简单方案：直接 EAT（有食物就吃，没食物也没关系）
    genome.push_back(ISA::EAT);

    // CMP R1, 3 ; R1 = nearestDistance
    // 与寄存器比较：比较 R1 和 R0，但需要先把3放入某寄存器
    // 这里简化：有临近实体就打
    addMovImm(5, 2);         // R5 = 2 (阈值)
    addCmp(1, 5);            // CMP R1, R5
    addJmp(ISA::JL, 0);      // 如果距离 < 2，跳转到战斗 (先占位)
    uint8_t fight_jmp_pos = static_cast<uint8_t>(genome.size() - 1);

    // CMP R6, 80 ; R6 = 自身能量
    addMovImm(5, 80);        // R5 = 80
    addCmp(6, 5);            // CMP R6, R5
    addJmp(ISA::JG, 0);      // 如果能量 > 80，跳转到复制 (先占位)
    uint8_t replicate_jmp_pos = static_cast<uint8_t>(genome.size() - 1);

    // 随机移动
    // R0 = rand() % 4
    addMovImm(0, static_cast<int32_t>(rng() % 4));
    genome.push_back(ISA::MOVE);

    // JMP loop
    addJmp(ISA::JMP, loop_start);

    // === fight 部分 ===
    uint8_t fight_start = static_cast<uint8_t>(genome.size());
    // 修正跳转地址
    genome[fight_jmp_pos] = fight_start;

    addMovReg(1, 3);         // R1 = R3 (最近实体方向)
    genome.push_back(ISA::FIGHT);
    addJmp(ISA::JMP, loop_start);

    // === replicate 部分 ===
    uint8_t replicate_start = static_cast<uint8_t>(genome.size());
    genome[replicate_jmp_pos] = replicate_start;

    genome.push_back(ISA::REPLICATE);
    addJmp(ISA::JMP, loop_start);

    return genome;
}

// ============================================================================
// 可视化
// ============================================================================
void DigitalLifeSimulator::render(std::ostream& out) {
    // 构建显示网格
    // 每个单元格: '.' = 空, '#' = 食物, '0'-'Z' = 实体 (按energy密度)
    const char densityChars[] = " .oO0@";

    out << "+" << std::string(Config::WORLD_WIDTH, '-') << "+\n";

    for (int y = 0; y < Config::WORLD_HEIGHT; ++y) {
        out << "|";
        for (int x = 0; x < Config::WORLD_WIDTH; ++x) {
            int32_t eid = m_world.entityGrid[y][x];
            int32_t food = m_world.foodGrid[y][x];

            if (eid >= 0 && m_ecs.isEntityAlive(eid)) {
                // 实体存在 - 按能量等级显示
                auto& energy = m_ecs.getEnergy(eid);
                float ratio = energy.ratio();
                char c;
                if (ratio > 0.8)       c = 'A' + static_cast<int>(ratio * 25);
                else if (ratio > 0.6)  c = '@';
                else if (ratio > 0.4)  c = 'O';
                else if (ratio > 0.2)  c = 'o';
                else                   c = '.';
                out << c;
            } else if (food > 0) {
                // 食物
                int level = (food * 5) / Config::MAX_FOOD_PER_TILE;
                if (level > 4) level = 4;
                out << densityChars[level];
            } else {
                out << ' ';
            }
        }
        out << "|\n";
    }
    out << "+" << std::string(Config::WORLD_WIDTH, '-') << "+";
}

void DigitalLifeSimulator::renderDetailed(std::ostream& out) {
    render(out);
    out << "\n";
    renderStats(out);
}

void DigitalLifeSimulator::renderStats(std::ostream& out) {
    int32_t aliveCount = m_ecs.getAliveCount();

    // 统计各代数量
    std::map<int32_t, int32_t> genCounts;
    int32_t totalEnergy = 0;
    int32_t maxGen = 0;
    int32_t totalGenomeSize = 0;

    auto ids = m_ecs.getAliveEntityIds();
    for (int32_t id : ids) {
        auto& age = m_ecs.getAge(id);
        auto& energy = m_ecs.getEnergy(id);
        genCounts[age.generation]++;
        totalEnergy += energy.energy;
        if (age.generation > maxGen) maxGen = age.generation;
        totalGenomeSize += static_cast<int32_t>(m_ecs.getGenome(id).code.size());
    }

    out << "====== 数字生命培养皿 统计 ======\n";
    out << "Tick: " << m_world.tickCount << "\n";
    out << "存活: " << aliveCount << " | 出生: " << m_world.totalBirths
        << " | 死亡: " << m_world.totalDeaths << "\n";
    out << "复制: " << m_world.totalReplications
        << " | 战斗: " << m_world.totalFights
        << " | 变异: " << m_world.totalMutations << "\n";
    out << "食物生成: " << m_world.totalFoodSpawned
        << " | 食物消耗: " << m_world.totalFoodConsumed << "\n";
    if (aliveCount > 0) {
        out << "平均能量: " << (totalEnergy / aliveCount)
            << " | 最大代数: " << maxGen
            << " | 平均基因组大小: " << (totalGenomeSize / aliveCount) << "B\n";
    }
    out << "代数分布: ";
    for (auto& [gen, cnt] : genCounts) {
        out << "G" << gen << ":" << cnt << " ";
    }
    out << "\n";
}

void DigitalLifeSimulator::renderEntityInfo(std::ostream& out,
                                             int32_t entityId) {
    if (!m_ecs.isEntityAlive(entityId)) {
        out << "实体 " << entityId << " 不存在或已死亡\n";
        return;
    }

    auto& age = m_ecs.getAge(entityId);
    auto& energy = m_ecs.getEnergy(entityId);
    auto& pos = m_ecs.getPosition(entityId);
    auto& vm = m_ecs.getVMState(entityId);
    auto& genome = m_ecs.getGenome(entityId);
    auto& sensor = m_ecs.getSensor(entityId);

    out << "===== 实体 #" << entityId << " =====\n";
    out << "代数: " << age.generation << " | 年龄: " << age.age
        << " | 父代: " << age.parentId << "\n";
    out << "能量: " << energy.energy << "/" << energy.maxEnergy
        << " (" << std::fixed << std::setprecision(1)
        << energy.ratio() * 100 << "%)\n";
    out << "位置: (" << pos.x << ", " << pos.y << ") 朝向: " << pos.facing << "\n";
    out << "基因组大小: " << genome.code.size() << "B"
        << " | 校验和: 0x" << std::hex << genome.checksum << std::dec << "\n";
    out << vm.disassemble() << "\n";
    out << "传感器: 最近E" << sensor.nearestEntityId
        << " d=" << sensor.nearestDistance
        << " e=" << sensor.nearestEnergy
        << " 食物=" << sensor.foodAtPosition
        << " 附近=" << sensor.entityCountNearby << "\n";
}
