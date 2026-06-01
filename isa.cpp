// ============================================================================
// 🖥️ ISA — 操作码名称实现
// ============================================================================
// 本文件实现了 op_name() 函数，将 8 位操作码映射为人类可读的助记符字符串。
// 主要用于：
//   - 反汇编输出（VMState::disassemble() 不直接使用此函数，但可用于调试）
//   - 调试日志中的指令跟踪
//   - 基因组的可读化展示
//
// 未知操作码（如未定义的 0x26-0xFE）返回 "???"，在 VM 执行时被当作 NOP 处理，
// 这是"容错执行"设计的关键——随机突变产生的非法操作码不会导致崩溃。
// ============================================================================
#include "isa.h"

const char* ISA::op_name(uint8_t op) {
    switch (op) {
        // ── 基础指令 ──
        case NOP:       return "NOP";       ///< 空操作
        case MOV:       return "MOV";       ///< 数据传送
        case ADD:       return "ADD";       ///< 加法
        case SUB:       return "SUB";       ///< 减法
        case MUL:       return "MUL";       ///< 乘法
        case DIV:       return "DIV";       ///< 除法
        case INC:       return "INC";       ///< 自增
        case DEC:       return "DEC";       ///< 自减
        case AND_OP:    return "AND";       ///< 按位与
        case OR_OP:     return "OR";        ///< 按位或
        case XOR_OP:    return "XOR";       ///< 按位异或
        case NOT_OP:    return "NOT";       ///< 按位取反
        case SHL:       return "SHL";       ///< 左移
        case SHR:       return "SHR";       ///< 右移

        // ── 比较与跳转 ──
        case CMP:       return "CMP";       ///< 比较
        case JMP:       return "JMP";       ///< 无条件跳转
        case JE:        return "JE";        ///< 相等跳转
        case JNE:       return "JNE";       ///< 不等跳转
        case JG:        return "JG";        ///< 大于跳转
        case JL:        return "JL";        ///< 小于跳转
        case JGE:       return "JGE";       ///< 大于等于跳转
        case JLE:       return "JLE";       ///< 小于等于跳转

        // ── 栈操作 ──
        case PUSH:      return "PUSH";      ///< 压栈
        case POP:       return "POP";       ///< 弹栈
        case CALL:      return "CALL";      ///< 子程序调用
        case RET:       return "RET";       ///< 子程序返回

        // ── 内存访问 ──
        case LOAD:      return "LOAD";      ///< 从数据内存加载
        case STORE:     return "STORE";     ///< 存入数据内存

        // ── 数字生命行为 ──
        case REPLICATE: return "REPLICATE"; ///< 自我复制
        case EAT:       return "EAT";       ///< 进食
        case FIGHT:     return "FIGHT";     ///< 战斗
        case SENSE:     return "SENSE";     ///< 感知环境
        case MOVE:      return "MOVE";      ///< 移动
        case DIE:       return "DIE";       ///< 自我终结

        // ── 停机 ──
        case HALT:      return "HALT";      ///< 停机

        default:        return "???";       ///< 未知操作码（如随机突变产生的非法值）
    }
}
