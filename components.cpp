// ============================================================================
// 🧩 ECS 纯数据组件 + VMState — 实现
// ============================================================================
//
// 本文件实现了所有组件的辅助方法和 VM 状态管理函数。
// 这些方法保持简单——大多数是纯数据操作，不含业务逻辑。
// 复杂的业务逻辑（如法则判定、战斗结算）放在 rule_system 和 systems 中。
// ============================================================================
#include "components.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// 🖥️ VMState — 虚拟机状态管理
// ============================================================================

VMState::VMState()
    : ip(0), sp(0), flags(0)
    , programMemoryBlock(-1), dataMemoryBlock(-1), stackMemoryBlock(-1)
    , halted(false)
{
    // 初始化所有寄存器为 0
    for (int i = 0; i < Config::NUM_REGISTERS; ++i) regs[i] = 0;
}

void VMState::reset() {
    // 清零所有寄存器（不释放内存池——内存池由 ECSManager 管理）
    for (int i = 0; i < Config::NUM_REGISTERS; ++i) regs[i] = 0;
    ip = 0; sp = 0; flags = 0; halted = false;
}

void VMState::loadGenome(const std::vector<uint8_t>& code) {
    genome = code;  // 复制基因组到本地副本
    reset();        // 重置执行状态（IP=0 从头开始执行）
}

std::string VMState::disassemble() const {
    std::stringstream ss;
    // 第一行：R0-R3
    ss << "Registers: ";
    for (int i = 0; i < 4; ++i) ss << "R" << i << "=" << std::setw(5) << regs[i] << " ";
    // 第二行：R4-R7（对齐输出）
    ss << "\n           ";
    for (int i = 4; i < 8; ++i) ss << "R" << i << "=" << std::setw(5) << regs[i] << " ";
    // 第三行：控制寄存器
    ss << "\nIP=" << ip << " SP=" << sp
       << " FLAGS=" << std::bitset<8>(flags)     // 以二进制显示标志位
       << " HALTED=" << (halted ? "Y" : "N");
    return ss.str();
}

// ============================================================================
// 🧬 GenomeComponent — CRC 计算
// ============================================================================

uint32_t GenomeComponent::computeChecksum() const {
    // 31 进制加权累加：sum = Σ (byte_i * 31^(n-1-i))
    // 选择 31 是因为它是质数，碰撞概率较低
    // 注意：这不是加密哈希，对于相同长度的不同序列可能碰撞
    uint32_t sum = 0;
    for (uint8_t b : code) sum = sum * 31 + b;
    return sum;
}

// ============================================================================
// ⚡ EnergyComponent — 能量收支
// ============================================================================

void EnergyComponent::spend(int32_t amount) {
    energy -= amount;
    energySpentThisTick += amount;  // 累积记录本 tick 总消耗
}

void EnergyComponent::gain(int32_t amount) {
    energy += amount;
    // 能量不能超过上限（防止无限累积）
    if (energy > maxEnergy) energy = maxEnergy;
}

// ============================================================================
// 📍 PositionComponent — 坐标与方向
// ============================================================================

void PositionComponent::move(int32_t direction) {
    // 只取低 2 位（direction ∈ {0,1,2,3}），防止非法方向值
    direction &= 3;
    switch (direction) {
        case 0: y = DL_WRAP_COORD(y - 1, Config::WORLD_HEIGHT); break;  // 上
        case 1: x = DL_WRAP_COORD(x + 1, Config::WORLD_WIDTH);  break;  // 右
        case 2: y = DL_WRAP_COORD(y + 1, Config::WORLD_HEIGHT); break;  // 下
        case 3: x = DL_WRAP_COORD(x - 1, Config::WORLD_WIDTH);  break;  // 左
    }
    facing = direction;  // 更新面朝方向
}

int32_t PositionComponent::distanceTo(const PositionComponent& o) const {
    // 环面上的 Chebyshev 距离：max(最短水平距离, 最短垂直距离)
    // 最短距离考虑环面绕行的两种可能路径
    int32_t dx = std::min(std::abs(x - o.x), Config::WORLD_WIDTH  - std::abs(x - o.x));
    int32_t dy = std::min(std::abs(y - o.y), Config::WORLD_HEIGHT - std::abs(y - o.y));
    return std::max(dx, dy);  // Chebyshev = 棋盘距离
}

int32_t PositionComponent::directionTo(const PositionComponent& o) const {
    // 计算环面上的最短方向向量
    int32_t dx = o.x - x, dy = o.y - y;
    // 如果直行距离 > 半周长，说明绕行更短，翻转方向
    if (std::abs(dx) > Config::WORLD_WIDTH  / 2) dx = -dx;
    if (std::abs(dy) > Config::WORLD_HEIGHT / 2) dy = -dy;
    // 返回绝对值较大的轴的方向（优先水平移动）
    return (std::abs(dx) > std::abs(dy)) ? ((dx > 0) ? 1 : 3) : ((dy > 0) ? 2 : 0);
}
