// ============================================================================
// 🗂️ 内存池管理 — 实现
// ============================================================================
//
// 实现一个简单的空闲链表内存池。分配策略：
//   1. 从 m_freeList 尾部弹出索引（LIFO）→ 最近释放的块最先被重用
//   2. 分配时自动清零 → 防止旧数据泄漏到新实体
//   3. 释放时仅将索引插回空闲链表 → O(1) 操作
//   4. 无碎片问题 → 所有块大小相同
//
// 为什么选择 LIFO 而非 FIFO？
//   - std::vector 的 back()/pop_back() 是 O(1) 且无内存移动
//   - 最近释放的块更可能在 CPU 缓存中（时间局部性）
//   - 简单实现，无需维护头尾指针
// ============================================================================
#include "memory_pool.h"
#include <cstring>

MemoryPool::MemoryPool(size_t blockSize, size_t maxBlocks)
    : m_blockSize(blockSize), m_maxBlocks(maxBlocks), m_usedCount(0), m_memory(nullptr)
{
    // 一次性分配全部内存（blockSize * maxBlocks 字节）
    size_t totalSize = blockSize * maxBlocks;
    m_memory = new uint8_t[totalSize];
    std::memset(m_memory, 0, totalSize);  // 初始化为零

    // 初始化空闲链表：将所有块索引按降序压入（0 是最后被分配的）
    m_freeList.reserve(maxBlocks);
    for (int32_t i = static_cast<int32_t>(maxBlocks) - 1; i >= 0; --i) {
        m_freeList.push_back(i);
    }
}

MemoryPool::~MemoryPool() {
    delete[] m_memory;   // 释放整块连续内存
    m_memory = nullptr;  // 防止悬空指针
}

int32_t MemoryPool::allocate() {
    // 池已满 → 返回 -1 表示失败
    if (m_freeList.empty()) return -1;

    // 从空闲链表尾部弹出（LIFO → O(1)）
    int32_t idx = m_freeList.back();
    m_freeList.pop_back();
    ++m_usedCount;

    // 清零块内容，防止旧数据泄漏
    std::memset(m_memory + idx * m_blockSize, 0, m_blockSize);
    return idx;
}

void MemoryPool::deallocate(int32_t blockIndex) {
    // 安全检查：忽略越界索引（防御性编程）
    if (blockIndex < 0 || blockIndex >= static_cast<int32_t>(m_maxBlocks)) return;

    // 将块索引插回空闲链表（LIFO → O(1)）
    m_freeList.push_back(blockIndex);

    // 更新使用计数（防止 underflow）
    if (m_usedCount > 0) --m_usedCount;
}

uint8_t* MemoryPool::getBlock(int32_t blockIndex) {
    // 越界检查
    if (blockIndex < 0 || blockIndex >= static_cast<int32_t>(m_maxBlocks)) return nullptr;

    // 计算块地址：基址 + 索引 × 块大小
    return m_memory + blockIndex * m_blockSize;
}

void MemoryPool::clear() {
    // 重置空闲链表
    m_freeList.clear();
    m_usedCount = 0;

    // 重新将所有块标记为空闲
    for (int32_t i = static_cast<int32_t>(m_maxBlocks) - 1; i >= 0; --i)
        m_freeList.push_back(i);

    // 物理清零全部内存
    std::memset(m_memory, 0, m_blockSize * m_maxBlocks);
}
