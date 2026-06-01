#pragma once
// ============================================================================
// 🧬 数字生命培养皿 (Digital Life Petri Dish) — 整体版头文件
// ============================================================================
//
// ⚠️ 重要说明：
//   本文件是整个项目的"整体版"（monolithic）头文件，包含所有模块的定义。
//   对应的实现文件为 digital_life_vm.cpp。
//
//   项目同时提供了模块化版本，每个模块有独立的 .h/.cpp 文件：
//     config.h        → 全局配置常量
//     isa.h/isa.cpp   → 极简指令集架构
//     memory_pool.h/cpp → 内存池管理
//     components.h/cpp  → 纯数据组件 + VM状态
//     world_state.h/cpp  → 世界状态
//     ecs_manager.h/cpp  → ECS 管理器
//     rule_system.h/cpp  → 法则系统
//     systems.h/cpp     → System 管线
//     simulator.h/cpp   → 顶层协调器
//
//   两种版本的功能完全一致。模块化版本更适合：
//     - 代码导航和理解
//     - 增量编译（修改一个模块不需要重新编译全部代码）
//   整体版更适合：
//     - 快速原型和单文件编译
//     - 将整个项目作为库嵌入其他项目
//
// 架构概览：
//   Config(常量) → ISA(指令集) → MemoryPool(内存) → Components(数据)
//       → WorldState(世界) → ECSManager(管理) → RuleSystem(法则)
//       → Systems(行为) → Simulator(顶层)
//
// 基于 ECS 架构的虚拟机，极简指令集，Tick 执行流
// 支持：自我复制、随机变异、能量争夺
// ============================================================================

#include <cstdint>
#include <vector>
#include <array>
#include <bitset>
#include <random>
#include <string>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <functional>
#include <unordered_map>

// ============================================================================
// 🌍 全局配置常量 — 所有可调参数
// ============================================================================
// 详见 config.h 中的详细注释
// ============================================================================
namespace Config {

// --- 世界参数 ---
constexpr int WORLD_WIDTH          = 64;        // 世界宽度
constexpr int WORLD_HEIGHT         = 64;        // 世界高度
constexpr int MAX_ENTITIES         = 512;       // 最大实体数
constexpr int MAX_FOOD_PER_TILE    = 10;        // 每格最大食物量

// --- VM 参数 ---
constexpr int PROGRAM_MEMORY_SIZE  = 256;       // 程序内存 (字节)
constexpr int STACK_SIZE           = 64;        // 栈深度
constexpr int DATA_MEMORY_SIZE     = 128;       // 数据内存 (字节)
constexpr int TICKS_PER_STEP       = 16;        // 每 tick 每实体执行的指令数
constexpr int NUM_REGISTERS        = 8;         // 通用寄存器数量

// --- 能量参数 ---
constexpr int INITIAL_ENERGY       = 100;       // 初始能量
constexpr int MAX_ENERGY           = 500;       // 最大能量
constexpr int ENERGY_DECAY_RATE    = 1;         // 每 tick 能量衰减
constexpr int MOVE_ENERGY_COST     = 2;         // 移动消耗
constexpr int REPLICATION_COST     = 50;        // 复制基础消耗
constexpr int REPLICATION_THRESHOLD = 80;       // 复制所需最低能量
constexpr int FIGHT_ENERGY_COST    = 10;        // 战斗消耗
constexpr int SENSE_ENERGY_COST    = 1;         // 感知消耗

// --- 变异参数 ---
constexpr double MUTATION_RATE     = 0.005;     // 每字节变异概率
constexpr double INSERTION_RATE    = 0.0005;    // 插入概率
constexpr double DELETION_RATE     = 0.0005;    // 删除概率

// --- 食物参数 ---
constexpr double FOOD_SPAWN_RATE   = 0.15;      // 每 tick 食物生成概率
constexpr int MAX_FOOD_ENERGY      = 30;        // 食物最大能量
constexpr int FOOD_ENERGY_PER_BITE = 15;        // 每次进食获取能量

// --- 寿命参数 ---
constexpr int MAX_AGE              = 2000;      // 最大寿命 (ticks)
constexpr int MAX_GENERATION       = 100;       // 最大代数

// --- 感知范围 ---
constexpr int SENSE_RANGE          = 5;         // 感知半径
}

// ============================================================================
// 🖥️ 极简指令集架构 (Minimal ISA) — 35 条指令
// ============================================================================
// 详见 isa.h 中的详细注释
// ============================================================================
namespace ISA {

// 操作码 (8-bit)
enum Opcode : uint8_t {
    NOP       = 0x00,  // 空操作
    MOV       = 0x01,  // MOV REG, IMM/REG
    ADD       = 0x02,  // ADD REG, REG
    SUB       = 0x03,  // SUB REG, REG
    MUL       = 0x04,  // MUL REG, REG
    DIV       = 0x05,  // DIV REG, REG (除数不为0)
    INC       = 0x06,  // INC REG
    DEC       = 0x07,  // DEC REG
    AND_OP    = 0x08,  // AND REG, REG
    OR_OP     = 0x09,  // OR  REG, REG
    XOR_OP    = 0x0A,  // XOR REG, REG
    NOT_OP    = 0x0B,  // NOT REG
    SHL       = 0x0C,  // SHL REG, IMM
    SHR       = 0x0D,  // SHR REG, IMM
    CMP       = 0x0E,  // CMP REG, REG (设置 FLAGS)
    JMP       = 0x0F,  // JMP ADDR
    JE        = 0x10,  // JE  ADDR (相等跳转)
    JNE       = 0x11,  // JNE ADDR (不等跳转)
    JG        = 0x12,  // JG  ADDR (大于跳转)
    JL        = 0x13,  // JL  ADDR (小于跳转)
    JGE       = 0x14,  // JGE ADDR (大于等于)
    JLE       = 0x15,  // JLE ADDR (小于等于)
    PUSH      = 0x16,  // PUSH REG
    POP       = 0x17,  // POP  REG
    CALL      = 0x18,  // CALL ADDR
    RET       = 0x19,  // RET
    LOAD      = 0x1A,  // LOAD REG, ADDR (从数据内存加载)
    STORE     = 0x1B,  // STORE ADDR, REG (存入数据内存)
    // --- 数字生命专用指令 ---
    REPLICATE = 0x20,  // 自我复制
    EAT       = 0x21,  // 进食/能量争夺
    FIGHT     = 0x22,  // 攻击
    SENSE     = 0x23,  // 感知周围环境
    MOVE      = 0x24,  // 移动
    DIE       = 0x25,  // 自我终结
    // ---
    HALT      = 0xFF   // 停机
};

// 寄存器编码 (3-bit -> 8个通用寄存器)
enum Register : uint8_t {
    R0 = 0, R1 = 1, R2 = 2, R3 = 3,
    R4 = 4, R5 = 5, R6 = 6, R7 = 7,
    // 特殊寄存器 (不可直接寻址)
    IP_REG = 8,    // 指令指针
    SP_REG = 9,    // 栈指针
    FLAGS_REG = 10 // 标志寄存器
};

// FLAGS 位
enum Flags : uint8_t {
    FLAG_ZERO     = 1 << 0,  // 零标志
    FLAG_CARRY    = 1 << 1,  // 进位/借位
    FLAG_NEGATIVE = 1 << 2,  // 负数标志
    FLAG_OVERFLOW = 1 << 3,  // 溢出标志
};

// 指令编码辅助
constexpr uint8_t encode_reg_reg(uint8_t op, uint8_t rd, uint8_t rs) {
    return (op << 6) | ((rd & 0x07) << 3) | (rs & 0x07);
}

constexpr uint8_t encode_reg_imm(uint8_t op, uint8_t rd, uint8_t imm) {
    return (op << 6) | ((rd & 0x07) << 3) | (imm & 0x07);
}

// 寄存器名称
inline const char* reg_name(uint8_t r) {
    static const char* names[] = {"R0","R1","R2","R3","R4","R5","R6","R7"};
    return (r < 8) ? names[r] : "??";
}

// 操作码名称
inline const char* op_name(uint8_t op) {
    switch (op) {
        case NOP:       return "NOP";
        case MOV:       return "MOV";
        case ADD:       return "ADD";
        case SUB:       return "SUB";
        case MUL:       return "MUL";
        case DIV:       return "DIV";
        case INC:       return "INC";
        case DEC:       return "DEC";
        case AND_OP:    return "AND";
        case OR_OP:     return "OR";
        case XOR_OP:    return "XOR";
        case NOT_OP:    return "NOT";
        case SHL:       return "SHL";
        case SHR:       return "SHR";
        case CMP:       return "CMP";
        case JMP:       return "JMP";
        case JE:        return "JE";
        case JNE:       return "JNE";
        case JG:        return "JG";
        case JL:        return "JL";
        case JGE:       return "JGE";
        case JLE:       return "JLE";
        case PUSH:      return "PUSH";
        case POP:       return "POP";
        case CALL:      return "CALL";
        case RET:       return "RET";
        case LOAD:      return "LOAD";
        case STORE:     return "STORE";
        case REPLICATE: return "REPLICATE";
        case EAT:       return "EAT";
        case FIGHT:     return "FIGHT";
        case SENSE:     return "SENSE";
        case MOVE:      return "MOVE";
        case DIE:       return "DIE";
        case HALT:      return "HALT";
        default:        return "???";
    }
}

} // namespace ISA
🗂️ 内存池管理 (Memory Pool) — 固定大小块分配器，空闲链表实现
// ============================================================================
// 详见 memory_pool.h 中的详细注释
// ============================================================================
// 内存池管理 (Memory Pool)
// ============================================================================
class MemoryPool {
public:
    MemoryPool(size_t blockSize, size_t maxBlocks);
    ~MemoryPool();

    // 分配一个内存块，返回块索引 (-1 表示失败)
    int32_t allocate();

    // 释放内存块
    void deallocate(int32_t blockIndex);

    // 获取块指针
    uint8_t* getBlock(int32_t blockIndex);

    // 获取块大小
    size_t blockSize() const { return m_blockSize; }

    // 已用/总量
    size_t usedBlocks() const { return m_usedCount; }
    size_t maxBlocks() const { return m_maxBlocks; }
    bool isFull() const { return m_usedCount >= m_maxBlocks; }

    // 清空
    void clear();

private:
    size_t m_blockSize;
    size_t m_maxBlocks;
    size_t m_usedCount;
    uint8_t* m_memory;         // 连续内存
    std::vector<int32_t> m_freeList; // 空闲块链表
};

// ============================================================================
// ============================================================================
// 🖥️ VM 状态 (每个实体的虚拟机核心)
// ============================================================================
// 包含 8 个通用寄存器 + IP/SP/FLAGS + 三块内存池索引
// ============================================================================
// VM 状态 (每个实体的虚拟机)
// ============================================================================
struct VMState {
    // 寄存器
    int32_t regs[Config::NUM_REGISTERS];  // R0-R7
    uint16_t ip;                        // 指令指针
    uint16_t sp;                        // 栈指针
    uint8_t  flags;                     // 标志寄存器

    // 内存池索引
    int32_t programMemoryBlock;         // 程序内存块
    int32_t dataMemoryBlock;            // 数据内存块
    int32_t stackMemoryBlock;           // 栈内存块

    // 基因组
    std::vector<uint8_t> genome;        // 基因组 (程序代码)

    // 执行状态
    bool halted;

    VMState();

    void reset();
    void loadGenome(const std::vector<uint8_t>& code);
    std::string disassemble() const;
// ============================================================================
// 🏷️ 组件类型掩码 — 32 位位掩码，每位对应一种组件
// ============================================================================
};

// ============================================================================
// ECS 组件 (纯数据)
// ============================================================================

// 组件类型掩码
enum ComponentType : uint32_t {
// ============================================================================
// 🧬 基因组组件 — 数字生命的"DNA"程序代码
// ============================================================================
    COMP_GENOME     = 1 << 0,   // 基因组
    COMP_ENERGY     = 1 << 1,   // 能量
    COMP_POSITION   = 1 << 2,   // 位置
    COMP_VMSTATE    = 1 << 3,   // VM状态
    COMP_AGE        = 1 << 4,   // 年龄
    COMP_ALIVE      = 1 << 5,   // 存活标记
// ============================================================================
// ⚡ 能量组件 — 生命的"经济货币"
// ============================================================================
    COMP_SENSOR     = 1 << 6,   // 传感器数据
};

// --- 基因组组件 ---
struct GenomeComponent {
    std::vector<uint8_t> code;    // 基因代码
    uint32_t checksum;            // 校验和 (用于快速比较)
    uint32_t parentChecksum;      // 父代校验和

    uint32_t computeChecksum() const;
};

// --- 能量组件 ---
struct EnergyComponent {
    int32_t energy;               // 当前能量
    int32_t maxEnergy;            // 最大能量
    int32_t energySpentThisTick;  // 本tick消耗

    void spend(int32_t amount);
    void gain(int32_t amount);
    bool isDead() const { return energy <= 0; }
    float ratio() const { return (float)energy / maxEnergy; }
};

// --- 位置组件 ---
// ============================================================================
// 📍 位置组件 — 2D 环面网格上的坐标（Chebyshev 距离）
// ============================================================================
struct PositionComponent {
    int32_t x, y;
    int32_t facing;               // 朝向 0=上 1=右 2=下 3=左

    void move(int32_t direction);
    int32_t distanceTo(const PositionComponent& other) const;
    int32_t directionTo(const PositionComponent& other) const;
// ============================================================================
// 🕰️ 年龄组件 — 生命历程：存活tick数 + 代数 + 父代ID
// ============================================================================
};

// --- 年龄组件 ---
struct AgeComponent {
    int32_t age;                  // 当前年龄 (ticks)
    int32_t generation;           // 代数
    int32_t parentId;             // 父实体ID (-1 表示原始)
// ============================================================================
// 👁️ 传感器组件 — 环境感知缓存（每 tick 由 SenseSystem 填充）
// ============================================================================
};

// --- 传感器组件 (每tick更新) ---
struct SensorComponent {
    int32_t nearestEntityId;      // 最近实体ID (-1 无)
    int32_t nearestDistance;      // 最近实体距离
    int32_t nearestEnergy;        // 最近实体能量
    int32_t nearestDirection;     // 最近实体方向
// ============================================================================
// 💀 存活标记组件 — 生死状态与死因记录
// ============================================================================
    int32_t foodAtPosition;       // 当前位置食物量
    int32_t entityCountNearby;    // 附近实体数
    int32_t avgEnergyNearby;      // 附近平均能量
};

// --- 存活标记 ---
// ============================================================================
// 🆔 实体 — 纯 ID + 组件掩码，不含任何行为逻辑
// ============================================================================
struct AliveComponent {
    bool alive;
    int32_t deathTick;            // 死亡 tick
    std::string deathCause;       // 死因
};

// ============================================================================
// 实体
// ============================================================================
struct Entity {
    static constexpr int32_t INVALID = -1;

    int32_t id;
    uint32_t componentMask;
    bool active;

    Entity() : id(INVALID), componentMask(0), active(false) {}
    explicit Entity(int32_t eid) : id(eid), componentMask(0), active(true) {}

    bool hasComponent(ComponentType type) const {
        return (componentMask & type) != 0;
// ============================================================================
// 🌍 世界状态 — 双网格系统（foodGrid + entityGrid）+ 全局统计
// ============================================================================
// foodGrid[y][x] = 食物量 | entityGrid[y][x] = 实体ID(-1=空)
// 世界使用环面拓扑，所有坐标通过 WRAP_COORD 自动包装
// ============================================================================
    }
    void addComponent(ComponentType type) {
        componentMask |= type;
    }
    void removeComponent(ComponentType type) {
        componentMask &= ~type;
    }
};

// ============================================================================
// 世界状态
// ============================================================================
struct WorldState {
    // 2D 网格上的食物能量
    int32_t foodGrid[Config::WORLD_HEIGHT][Config::WORLD_WIDTH];

    // 实体占用网格 (存储实体ID，-1表示空)
    int32_t entityGrid[Config::WORLD_HEIGHT][Config::WORLD_WIDTH];

    // 全局 tick 计数
    uint64_t tickCount;

    // 统计
    uint64_t totalBirths;
    uint64_t totalDeaths;
    uint64_t totalFights;
    uint64_t totalReplications;
    uint64_t totalMutations;
    uint64_t totalFoodSpawned;
    uint64_t totalFoodConsumed;

    WorldState();

    void reset();
    void spawnFood(std::mt19937& rng);
    bool isOccupied(int32_t x, int32_t y) const;
    int32_t getFood(int32_t x, int32_t y) const;
    int32_t consumeFood(int32_t x, int32_t y, int32_t amount);
    void addFood(int32_t x, int32_t y, int32_t amount);
    void setEntity(int32_t x, int32_t y, int32_t entityId);
    void clearEntity(int32_t x, int32_t y);
    int32_t getEntity(int32_t x, int32_t y) const;
};

// ============================================================================
// 法则系统 (Rule System)
// ============================================================================
// ============================================================================
// ⚖️ 法则系统 — 能量守恒、变异概率、战斗结算、寿命判定
// ============================================================================
// 所有方法均为静态——RuleSystem 是无状态的纯函数集合
// ============================================================================
struct RuleSystem {
    // 能量法则
    static int32_t calculateDecay(int32_t energy, int32_t age);
    static int32_t calculateReplicationCost(int32_t parentEnergy);
    static int32_t calculateFightResult(int32_t attackerEnergy, int32_t defenderEnergy);
    static int32_t calculateEnergyGainFromFood(int32_t foodAmount);

    // 变异法则
    static double effectiveMutationRate(int32_t generation, int32_t energy);
    static void applyMutations(std::vector<uint8_t>& genome, std::mt19937& rng,
                               int32_t generation, int32_t energy);

    // 年龄法则
    static bool shouldDieOfAge(int32_t age, int32_t generation);
    static int32_t maxAgeForGeneration(int32_t generation);

    // 战斗法则
    static bool fightResolve(int32_t attackerEnergy, int32_t defenderEnergy,
                             int32_t& energyTransfer, bool& attackerWins);

    // 法则验证
    static bool canReplicate(int32_t energy, int32_t age, int32_t generation,
                             int32_t nearbyCount);
    static bool canFight(int32_t energy);
    static bool canMove(int32_t energy);
    static bool canSense(int32_t energy);
};

// ============================================================================
// ECS 管理器
// ============================================================================
// ============================================================================
// 🏗️ ECS 管理器 — 实体生命周期 + 组件存取（SoA布局）+ 三个内存池
// ============================================================================
class ECSManager {
public:
    ECSManager();
    ~ECSManager();

    // 实体管理
    int32_t createEntity();
    void destroyEntity(int32_t entityId);
    bool isEntityAlive(int32_t entityId) const;
    Entity& getEntity(int32_t entityId);
    const Entity& getEntity(int32_t entityId) const;
    int32_t getEntityCount() const { return m_entityCount; }

    // 组件管理
    GenomeComponent&    getGenome(int32_t entityId);
    EnergyComponent&    getEnergy(int32_t entityId);
    PositionComponent&  getPosition(int32_t entityId);
    VMState&            getVMState(int32_t entityId);
    AgeComponent&       getAge(int32_t entityId);
    AliveComponent&     getAlive(int32_t entityId);
    SensorComponent&    getSensor(int32_t entityId);

    bool hasComponent(int32_t entityId, ComponentType type) const;

    // 迭代活着的实体
    template<typename Func>
    void forEachAlive(Func&& func) {
        for (int32_t i = 0; i < Config::MAX_ENTITIES; ++i) {
            if (m_entities[i].active && m_alive[i].alive) {
                func(i, m_entities[i]);
            }
        }
    }

    // 获取所有存活实体ID
    std::vector<int32_t> getAliveEntityIds() const;

    // 内存池
    MemoryPool& getProgramMemory() { return *m_programPool; }
    MemoryPool& getDataMemory()    { return *m_dataPool; }
    MemoryPool& getStackMemory()   { return *m_stackPool; }

    // 统计
    int32_t getAliveCount() const;

private:
    std::array<Entity,           Config::MAX_ENTITIES> m_entities;
    std::array<GenomeComponent,  Config::MAX_ENTITIES> m_genomes;
    std::array<EnergyComponent,  Config::MAX_ENTITIES> m_energies;
    std::array<PositionComponent,Config::MAX_ENTITIES> m_positions;
    std::array<VMState,          Config::MAX_ENTITIES> m_vmStates;
    std::array<AgeComponent,     Config::MAX_ENTITIES> m_ages;
    std::array<AliveComponent,   Config::MAX_ENTITIES> m_alive;
    std::array<SensorComponent,  Config::MAX_ENTITIES> m_sensors;

    int32_t m_entityCount;

    MemoryPool* m_programPool;
    MemoryPool* m_dataPool;
    MemoryPool* m_stackPool;

    int32_t m_nextId;
    std::vector<int32_t> m_freeIds;
};

// ============================================================================
// 🔄 System 基类 — 所有系统的抽象接口
// ============================================================================
// ============================================================================
// System 基础类
// ============================================================================
class SystemBase {
public:
    virtual ~SystemBase() = default;
    virtual void execute(ECSManager& ecs, WorldState& world,
                         std::mt19937& rng) = 0;
    virtual const char* name() const = 0;
};

// ============================================================================
// 代谢系统 (能量衰减)
// ============================================================================
class MetabolismSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Metabolism"; }
};

// ============================================================================
// 感知系统
// ============================================================================
class SenseSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Sense"; }
};

// ============================================================================
// VM 执行系统 (运行基因组程序)
// ============================================================================
class VMExecutionSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "VMExecution"; }

private:
    // 执行单条指令，返回 true 表示继续执行
    bool executeInstruction(ECSManager& ecs, WorldState& world,
                            int32_t entityId, std::mt19937& rng);
    // 获取指令操作码
    uint8_t fetchOpcode(VMState& vm, int32_t entityId, ECSManager& ecs);
    // 读取操作数
    int32_t fetchImmediate(VMState& vm, int32_t entityId, ECSManager& ecs);
};

// ============================================================================
// 移动系统
// ============================================================================
class MovementSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Movement"; }

    // 由 VM 调用
    static bool moveEntity(ECSManager& ecs, WorldState& world,
                           int32_t entityId, int32_t direction);
};

// ============================================================================
// 进食系统
// ============================================================================
class EatingSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Eating"; }

    // 由 VM 调用
    static bool eatAtPosition(ECSManager& ecs, WorldState& world,
                              int32_t entityId);
};

// ============================================================================
// 战斗系统
// ============================================================================
class CombatSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Combat"; }

    // 由 VM 调用
    static bool fightEntity(ECSManager& ecs, WorldState& world,
                            int32_t attackerId, int32_t targetId);
};

// ============================================================================
// 复制系统
// ============================================================================
class ReplicationSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
    const char* name() const override { return "Replication"; }

    // 由 VM 调用
    static int32_t replicateEntity(ECSManager& ecs, WorldState& world,
                                   int32_t parentId, std::mt19937& rng);
};

// ============================================================================
// 死亡清理系统
// ============================================================================
class DeathSystem : public SystemBase {
public:
    void execute(ECSManager& ecs, WorldState& world,
                 std::mt19937& rng) override;
// ============================================================================
// 🎮 数字生命模拟器 — 顶层协调器
// ============================================================================
// 持有 ECSManager + WorldState + RNG + System管线
// 驱动 Tick 循环、世界初始化、基因组生成、可视化
// ============================================================================
    const char* name() const override { return "Death"; }
};

// ============================================================================
// 数字生命模拟器 (培养皿)
// ============================================================================
class DigitalLifeSimulator {
public:
    DigitalLifeSimulator();

    // 初始化世界
    void initialize(std::mt19937& rng);

    // 执行一个 tick
    void tick();

    // 运行多个 tick
    void run(int32_t numTicks, bool verbose = false);

    // 生成随机基因组
    static std::vector<uint8_t> generateRandomGenome(std::mt19937& rng,
                                                      size_t length = 64);

    // 生成更智能的种子基因组
    static std::vector<uint8_t> generateSeedGenome(std::mt19937& rng);

    // 生成原始生命 (第一批实体)
    int32_t spawnPrimordialLife(std::mt19937& rng,
                                int32_t x, int32_t y,
                                const std::vector<uint8_t>& genome);

    // 获取状态
    ECSManager& getECS() { return m_ecs; }
    const ECSManager& getECS() const { return m_ecs; }
    WorldState& getWorld() { return m_world; }
    const WorldState& getWorld() const { return m_world; }

    // 可视化
    void render(std::ostream& out);
    void renderDetailed(std::ostream& out);
    void renderStats(std::ostream& out);
    void renderEntityInfo(std::ostream& out, int32_t entityId);

    // 获取随机引擎
    std::mt19937& getRNG() { return m_rng; }

private:
    ECSManager m_ecs;
    WorldState m_world;
    RuleSystem m_rules;
    std::mt19937 m_rng;

    // 系统管线 (按顺序执行)
    std::vector<SystemBase*> m_systems;

    void setupSystems();
};
