#pragma once
// ============================================================================
// 🧩 ECS 纯数据组件 + VM状态 + 实体定义 — 数字生命培养皿
// ============================================================================
//
// 架构说明：
//   本项目采用 ECS (Entity-Component-System) 架构的简化变体。
//
//   Entity   — 仅为一个整数 ID + 组件掩码，不含任何行为逻辑
//   Component — 纯数据结构（Plain Old Data），不含任何方法（除少量辅助函数）
//   System   — 包含所有行为逻辑，对拥有特定组件的实体执行操作
//
// 组件数据流（一个 Tick 内的处理顺序）：
//   ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
//   │Metabolism│───▶│  Sense   │───▶│VMExecute │───▶│ Movement │
//   │(能量衰减)│    │(填充传感器)│   │(执行基因组)│   │(网格同步)│
//   └──────────┘    └──────────┘    └──────────┘    └──────────┘
//        │                                                  │
//        ▼                                                  ▼
//   ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
//   │  Death   │◀───│Replicate │◀───│  Combat  │◀───│  Eating  │
//   │(清理死亡)│    │(自我复制)│    │(能量争夺)│    │(消耗食物)│
//   └──────────┘    └──────────┘    └──────────┘    └──────────┘
//
// 组件与 System 的对应关系：
//   Metabolism  → Energy, Age, Alive
//   Sense       → Position, Sensor
//   VMExecute   → VMState, Genome, Sensor, Energy, Age, Position
//   Movement    → Position, Energy
//   Eating      → Position, Energy, WorldState
//   Combat      → Energy, Position, Alive
//   Replication → Genome, Energy, Age, Position, Sensor, VMState
//   Death       → Age, Energy, Alive, Position
// ============================================================================

#include "config.h"
#include <cstdint>
#include <vector>
#include <string>
#include <bitset>
#include <sstream>
#include <iomanip>

// ============================================================================
// 🖥️ VM 状态 — 每个实体的虚拟机核心
// ============================================================================
//
// VMState 是数字生命的"大脑"。它包含：
//   - 8 个 32 位通用寄存器 (R0-R7)
//   - 程序计数器 (IP)、栈指针 (SP)、标志寄存器 (FLAGS)
//   - 三块内存池的索引（程序/数据/栈）
//   - 基因组的本地副本（genome vector）
//
// 注意：genome 是冗余存储——程序内存池（programMemoryBlock）中也有一份。
// 保留 genome vector 是为了方便反汇编、变异操作和调试输出，
// 避免频繁通过 MemoryPool 接口读写。
// ============================================================================
struct VMState {
    int32_t  regs[Config::NUM_REGISTERS];  ///< R0-R7 通用寄存器（32位有符号）
    uint16_t ip;                            ///< 指令指针 — 指向 genome 中的下一条指令
    uint16_t sp;                            ///< 栈指针 — 指向栈顶下一个空闲槽位
    uint8_t  flags;                         ///< 标志寄存器 — CMP 结果 (Z/C/N/V)

    int32_t programMemoryBlock;             ///< 程序内存池中的块索引（-1=未分配）
    int32_t dataMemoryBlock;                ///< 数据内存池中的块索引
    int32_t stackMemoryBlock;               ///< 栈内存池中的块索引

    std::vector<uint8_t> genome;            ///< 基因组副本 — 便于调试和变异操作
    bool halted;                            ///< 是否已停机（IP 不再前进）

    VMState();

    /// @brief 重置 VM 到初始状态（寄存器=0, IP=0, SP=0, 未停机）
    void reset();

    /// @brief 加载基因组并重置 VM
    /// @param code 基因组字节序列
    void loadGenome(const std::vector<uint8_t>& code);

    /// @brief 反汇编当前 VM 状态为可读字符串
    /// @return 包含寄存器值、IP、SP、FLAGS 的格式化字符串
    std::string disassemble() const;
};

// ============================================================================
// 🏷️ 组件类型掩码 — 32 位位掩码，每位对应一种组件
// ============================================================================
//
// 使用位掩码而非继承体系的原因：
//   1. 快速检查实体是否拥有某组件（O(1) 位运算）
//   2. System 可高效筛选拥有特定组件组合的实体
//   3. 避免虚函数调用开销
//   4. 内存紧凑（每个实体仅需 4 字节掩码）
//
// 注意：最多支持 32 种组件类型。当前使用 7 种，扩展空间充裕。
// ============================================================================
enum ComponentType : uint32_t {
    COMP_GENOME   = 1 << 0,  ///< 基因组 — 实体的"DNA"程序代码
    COMP_ENERGY   = 1 << 1,  ///< 能量 — 生命的经济货币
    COMP_POSITION = 1 << 2,  ///< 位置 — 在 2D 环面网格上的坐标
    COMP_VMSTATE  = 1 << 3,  ///< VM 状态 — 虚拟机的寄存器/内存
    COMP_AGE      = 1 << 4,  ///< 年龄 — 出生代数与存活 tick 数
    COMP_ALIVE    = 1 << 5,  ///< 存活标记 — 是否存活及死因
    COMP_SENSOR   = 1 << 6,  ///< 传感器 — 感知周围环境的缓存
};

// ============================================================================
// 🧬 基因组组件 — 数字生命的"DNA"
// ============================================================================
//
// 基因组就是程序代码——一个字节序列，被 VM 逐条解释执行。
// checksum 用于快速比较两个基因组是否相同（检测变异是否发生）。
// parentChecksum 记录父代的校验和，用于追踪进化谱系。
// ============================================================================
struct GenomeComponent {
    std::vector<uint8_t> code;      ///< 程序字节码（即 ISA 指令序列）
    uint32_t checksum;              ///< 当前基因组的 CRC（31进制加权和）
    uint32_t parentChecksum;        ///< 父代基因组 CRC（0=原始生命）

    /// @brief 计算基因组的 CRC 校验和
    /// @details 使用 31 进制加权累加：sum = sum * 31 + byte
    ///          这不是密码学安全的哈希，但适合快速比较
    /// @return 32 位校验和
    uint32_t computeChecksum() const;
};

// ============================================================================
// ⚡ 能量组件 — 数字生命的"经济货币"
// ============================================================================
//
// 能量是所有行为的约束条件：
//   - 移动、感知、战斗、复制都需要消耗能量
//   - 每 tick 有基础代谢衰减（随年龄增加）
//   - 进食可从环境获取能量
//   - 战斗胜利可夺取对手能量
//   - 能量 ≤ 0 → 死亡
//
// energySpentThisTick 用于追踪单个 tick 内的总消耗，
// 便于调试和统计分析（"这个实体花了多少能量在行动上？"）
// ============================================================================
struct EnergyComponent {
    int32_t energy;                 ///< 当前能量值（≤0 即死亡）
    int32_t maxEnergy;              ///< 能量上限（防止无限累积）
    int32_t energySpentThisTick;    ///< 本 tick 已消耗的能量总和

    /// @brief 消耗能量
    /// @param amount 消耗量（正值）
    void spend(int32_t amount);

    /// @brief 获取能量（进食/战斗胜利）
    /// @param amount 获取量，不超过 maxEnergy
    void gain(int32_t amount);

    /// @brief 能量是否耗尽（即是否应该死亡）
    bool  isDead() const { return energy <= 0; }

    /// @brief 能量比例 [0.0, 1.0]，用于可视化颜色映射
    float ratio()  const { return static_cast<float>(energy) / maxEnergy; }
};

// ============================================================================
// 📍 位置组件 — 2D 环面网格上的坐标
// ============================================================================
//
// 世界是环面（toroidal）：x 方向左右边界相连，y 方向上下边界相连。
// 所有坐标操作通过 DL_WRAP_COORD 宏自动处理边界包装。
//
// 距离使用 Chebyshev 度量：max(|dx|, |dy|)
// 这种度量适合网格世界——对角线移动和直线移动"花费相同步数"。
//
// 方向编码：0=上 1=右 2=下 3=左 (顺时针方向)
// ============================================================================
struct PositionComponent {
    int32_t x, y;           ///< 当前坐标 [0, WORLD_WIDTH) × [0, WORLD_HEIGHT)
    int32_t facing;         ///< 面朝方向 (0=上, 1=右, 2=下, 3=左)

    /// @brief 向指定方向移动一格（自动处理环面边界）
    /// @param direction 方向 (0-3)，超出部分取低2位
    /// @note 移动后自动更新 facing 字段
    void    move(int32_t direction);

    /// @brief 计算到另一个实体的 Chebyshev 距离
    /// @param other 目标位置
    /// @return 环面最短距离 = max(min(|dx|, W-|dx|), min(|dy|, H-|dy|))
    int32_t distanceTo(const PositionComponent& other) const;

    /// @brief 计算指向另一个实体的方向
    /// @param other 目标位置
    /// @return 方向值 (0-3)，优先选择差值绝对值较大的轴
    int32_t directionTo(const PositionComponent& other) const;
};

// ============================================================================
// 🕰️ 年龄组件 — 生命历程记录
// ============================================================================
//
// generation 是进化深度的度量：
//   G0 = 原始生命（由模拟器 seed 生成）
//   G1 = 原始生命的直接后代
//   Gn = 第 n 代
//
// parentId = -1 表示原始生命（无父代），这是追踪进化树的根节点标记。
// ============================================================================
struct AgeComponent {
    int32_t age;            ///< 存活 tick 数（从出生算起）
    int32_t generation;     ///< 进化代数（0=原始生命）
    int32_t parentId;       ///< 父实体 ID（-1=原始生命，无父代）
};

// ============================================================================
// 👁️ 传感器组件 — 环境感知缓存
// ============================================================================
//
// 每 tick 由 SenseSystem 填充，供 VM 的 SENSE 指令读取。
// 传感器数据是"快照"——在一次 tick 内保持不变，
// 确保同一 tick 内多次 SENSE 调用获得一致的结果。
//
// 初始值设计：
//   nearestDistance=99 表示"无邻近实体"（99 > SENSE_RANGE=5）
//   nearestEntityId=-1 表示"未探测到实体"
// ============================================================================
struct SensorComponent {
    int32_t nearestEntityId  = -1;  ///< 最近实体的 ID（-1=无）
    int32_t nearestDistance  = 99;  ///< 到最近实体的 Chebyshev 距离
    int32_t nearestEnergy    = 0;   ///< 最近实体的当前能量
    int32_t nearestDirection = 0;   ///< 最近实体所在方向 (0-3)
    int32_t foodAtPosition   = 0;   ///< 当前位置的食物量
    int32_t entityCountNearby = 0;  ///< 感知范围内的实体数量（不含自己）
    int32_t avgEnergyNearby  = 0;   ///< 附近实体的平均能量
};

// ============================================================================
// 💀 存活标记组件 — 生死状态
// ============================================================================
//
// 注意：alive=false 不代表实体已被销毁（从数组中移除）。
// 死亡实体先被标记为 alive=false，在 DeathSystem 结束时统一清理。
// 这种"延迟销毁"模式避免了在遍历过程中修改集合导致的迭代器失效。
//
// deathCause 是调试友好的字符串，记录死因：
//   "energy_depleted"  — 能量耗尽
//   "old_age"          — 自然老死
//   "killed_in_combat" — 战斗中阵亡
//   "self_termination" — 执行 DIE 指令自杀
// ============================================================================
struct AliveComponent {
    bool alive = false;             ///< 是否存活
    int32_t deathTick = 0;          ///< 死亡发生的 tick 编号
    std::string deathCause;         ///< 死因描述（调试用）
};

// ============================================================================
// 🆔 实体 — 仅包含 ID 和组件掩码的最小化结构
// ============================================================================
//
// Entity 不存储任何组件数据——所有数据在 ECSManager 的组件数组中按索引存取。
// 这是"纯 ECS"风格：Entity 只是一个轻量级的标识符。
//
// INVALID = -1 作为"空实体"标记，类似于 nullptr。
// ============================================================================
struct Entity {
    static constexpr int32_t INVALID = -1;  ///< 无效实体 ID 常量

    int32_t  id;                ///< 实体唯一标识符（同时是组件数组的索引）
    uint32_t componentMask;     ///< 组件位掩码（哪些组件被激活）
    bool     active;            ///< 槽位是否被占用（false=可回收）

    /// @brief 默认构造 — 创建无效实体
    Entity() : id(INVALID), componentMask(0), active(false) {}

    /// @brief 用给定 ID 构造实体
    explicit Entity(int32_t eid) : id(eid), componentMask(0), active(true) {}

    /// @brief 检查实体是否拥有指定类型的组件
    bool hasComponent(ComponentType type) const { return (componentMask & type) != 0; }

    /// @brief 添加组件类型标记
    void addComponent(ComponentType type)    { componentMask |= type; }

    /// @brief 移除组件类型标记
    void removeComponent(ComponentType type) { componentMask &= ~type; }
};
