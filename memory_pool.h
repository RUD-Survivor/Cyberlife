#pragma once
// ============================================================================
// 🗂️ 内存池管理 (Memory Pool) — 数字生命培养皿
// ============================================================================
//
// 固定大小块分配器，基于空闲链表（Free List）实现。
//
// 设计动机：
//   数字生命项目中每个实体需要三块固定大小的内存：
//     - 程序内存 (PROGRAM_MEMORY_SIZE = 256B) — 存储基因组/指令
//     - 数据内存 (DATA_MEMORY_SIZE = 128B)    — 通用读写存储
//     - 栈内存   (STACK_SIZE * 4 = 256B)     — 调用栈
//   由于实体频繁创建/销毁（复制 → 新实体），使用系统 malloc/free 会导致：
//     1. 内存碎片化
//     2. 不确定的分配延迟
//     3. 无法简单追踪内存使用
//
// 解决方案：
//   预分配一大块连续内存，划分为等大小的块。使用 std::vector<int32_t>
//   作为空闲链表（LIFO 顺序，即从 vector 尾部弹出），实现 O(1) 分配/释放。
//
// 内存布局：
//   ┌───────────────────────────────────────────────────┐
//   │ Block 0 │ Block 1 │ Block 2 │ ... │ Block N-1    │
//   │ (size B) │ (size B) │ (size B) │     │ (size B)    │
//   └───────────────────────────────────────────────────┘
//   每个块大小 = m_blockSize 字节，共 m_maxBlocks 个块
//   块地址 = m_memory + blockIndex * m_blockSize
//
// 使用索引而非指针：
//   返回 int32_t 块索引而非 uint8_t* 指针。优点：
//     - 索引可安全存储在 VMState 中（programMemoryBlock 等字段）
//     - 避免悬空指针问题（实体销毁后索引显式释放）
//     - 方便调试和日志输出
// ============================================================================

#include <cstdint>
#include <vector>

class MemoryPool {
public:
    /// @brief 构造内存池
    /// @param blockSize 每个块的大小（字节）
    /// @param maxBlocks 最大块数量（即最多同时分配多少块）
    /// @note 总内存 = blockSize * maxBlocks 字节，在构造函数中一次性分配
    MemoryPool(size_t blockSize, size_t maxBlocks);

    /// @brief 析构 — 释放整个内存池
    ~MemoryPool();

    /// @brief 分配一个内存块
    /// @return 块索引 (0 ~ maxBlocks-1)，如果池已满则返回 -1
    /// @note O(1) 时间复杂度，分配后自动清零
    int32_t allocate();

    /// @brief 释放之前分配的内存块
    /// @param blockIndex 要释放的块索引（由 allocate() 返回）
    /// @note 越界的索引会被静默忽略（安全设计）
    void deallocate(int32_t blockIndex);

    /// @brief 获取块的原始指针（用于读写块内容）
    /// @param blockIndex 块索引
    /// @return 指向块起始地址的指针，无效索引返回 nullptr
    /// @note 调用者负责确保索引有效。返回的指针在池析构后失效。
    uint8_t* getBlock(int32_t blockIndex);

    /// @brief 获取单个块的大小（字节）
    size_t blockSize() const { return m_blockSize; }

    /// @brief 当前已分配的块数量
    size_t usedBlocks() const { return m_usedCount; }

    /// @brief 池的最大容量（块数）
    size_t maxBlocks() const { return m_maxBlocks; }

    /// @brief 检查池是否已满（无法再分配）
    bool   isFull()     const { return m_usedCount >= m_maxBlocks; }

    /// @brief 清空内存池，回收所有块
    /// @note 将所有块标记为空闲，并将所有内存清零
    void clear();

private:
    size_t   m_blockSize;                ///< 每个块的大小（字节）
    size_t   m_maxBlocks;                ///< 块的总数量
    size_t   m_usedCount;                ///< 当前已使用的块数量
    uint8_t* m_memory;                   ///< 连续内存块（一次性分配，永不重新分配）
    std::vector<int32_t> m_freeList;     ///< 空闲块索引列表（LIFO 栈结构）
};
