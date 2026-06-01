// ============================================================================
// 🌍 世界状态 — 实现
// ============================================================================
//
// 实现要点：
//   1. 所有坐标操作都经过环面包装（DL_WRAP_COORD）
//      ——即使调用者传入越界坐标也不会崩溃
//   2. 食物操作保证不超过 MAX_FOOD_PER_TILE 上限
//   3. 食物消耗返回实际消耗量（可能小于请求量）
//   4. entityGrid 使用 -1 表示空（方便条件判断：>=0 即被占用）
// ============================================================================
#include "world_state.h"
#include <random>
#include <algorithm>

WorldState::WorldState()
    : tickCount(0), totalBirths(0), totalDeaths(0), totalFights(0)
    , totalReplications(0), totalMutations(0)
    , totalFoodSpawned(0), totalFoodConsumed(0)
{
    reset();  // 初始化所有网格为零
}

void WorldState::reset() {
    // 清零食物网格和实体网格
    for (int y = 0; y < Config::WORLD_HEIGHT; ++y)
        for (int x = 0; x < Config::WORLD_WIDTH; ++x)
            { foodGrid[y][x] = 0; entityGrid[y][x] = -1; }  // -1 = 空

    // 重置所有统计计数器
    tickCount = totalBirths = totalDeaths = totalFights = 0;
    totalReplications = totalMutations = totalFoodSpawned = totalFoodConsumed = 0;
}

void WorldState::spawnFood(std::mt19937& rng) {
    // 分布生成器
    std::uniform_int_distribution<int32_t> xDist(0, Config::WORLD_WIDTH  - 1);
    std::uniform_int_distribution<int32_t> yDist(0, Config::WORLD_HEIGHT - 1);
    std::uniform_int_distribution<int32_t> amt(5, Config::MAX_FOOD_ENERGY);  // 每个食物 5~30 能量

    // 计算生成次数：WORLD_WIDTH * WORLD_HEIGHT * FOOD_SPAWN_RATE
    // 例：64*64*0.15 ≈ 614 次/tick，至少 1 次
    int32_t n = std::max(1, static_cast<int32_t>(Config::WORLD_WIDTH * Config::WORLD_HEIGHT * Config::FOOD_SPAWN_RATE));

    for (int i = 0; i < n; ++i) {
        int32_t fx = xDist(rng), fy = yDist(rng), fa = amt(rng);
        // 累加食物，但不超过上限（使用 std::min 截断）
        foodGrid[fy][fx] = std::min(foodGrid[fy][fx] + fa, Config::MAX_FOOD_PER_TILE);
        totalFoodSpawned += fa;  // 统计总生成量（包括被截断的部分）
    }
}

bool WorldState::isOccupied(int32_t x, int32_t y) const {
    // 环面坐标包装后检查
    return entityGrid[DL_WRAP_COORD(y, Config::WORLD_HEIGHT)][DL_WRAP_COORD(x, Config::WORLD_WIDTH)] >= 0;
}

int32_t WorldState::getFood(int32_t x, int32_t y) const {
    return foodGrid[DL_WRAP_COORD(y, Config::WORLD_HEIGHT)][DL_WRAP_COORD(x, Config::WORLD_WIDTH)];
}

int32_t WorldState::consumeFood(int32_t x, int32_t y, int32_t amount) {
    x = DL_WRAP_COORD(x, Config::WORLD_WIDTH);
    y = DL_WRAP_COORD(y, Config::WORLD_HEIGHT);

    // 实际消耗量 ≤ min(请求量, 现有量)
    int32_t c = std::min(amount, foodGrid[y][x]);
    foodGrid[y][x] -= c;
    totalFoodConsumed += c;  // 统计
    return c;                 // 返回实际消耗量（调用方据此增加能量）
}

void WorldState::addFood(int32_t x, int32_t y, int32_t amount) {
    x = DL_WRAP_COORD(x, Config::WORLD_WIDTH);
    y = DL_WRAP_COORD(y, Config::WORLD_HEIGHT);
    // 累加但不超过每格上限（防止单一格子食物无限堆积）
    foodGrid[y][x] = std::min(foodGrid[y][x] + amount, Config::MAX_FOOD_PER_TILE);
}

void WorldState::setEntity(int32_t x, int32_t y, int32_t eid) {
    entityGrid[DL_WRAP_COORD(y, Config::WORLD_HEIGHT)][DL_WRAP_COORD(x, Config::WORLD_WIDTH)] = eid;
}

void WorldState::clearEntity(int32_t x, int32_t y) {
    setEntity(x, y, -1);  // -1 表示"无实体占用"
}

int32_t WorldState::getEntity(int32_t x, int32_t y) const {
    return entityGrid[DL_WRAP_COORD(y, Config::WORLD_HEIGHT)][DL_WRAP_COORD(x, Config::WORLD_WIDTH)];
}
