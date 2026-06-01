// ============================================================================
// 数字生命培养皿 - 演示主程序
// 基于 ECS 架构虚拟机 | 极简指令集 | Tick 执行流
// 数字生命: 自我复制 · 随机变异 · 能量争夺
// ============================================================================

#include "simulator.h"   // 顶层: 包含所有模块化头文件
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <conio.h>       // Windows: _kbhit, _getch
#define NOMINMAX
#include <windows.h>     // Windows: SetConsoleMode

// ============================================================================
// 🎨 控制台颜色辅助 — Windows Console API 封装
// ============================================================================
namespace Console {

/// @brief 设置控制台前景色
/// @param fg Windows 颜色属性值 (如 7=白色, 10=绿色, 14=黄色)
void setColor(int fg) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(h, static_cast<WORD>(fg));
}

/// @brief 重置为默认颜色 (白色=7)
void reset() { setColor(7); }

/// @brief 清屏（填充空格 + 重置光标到左上角）
void clear() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    DWORD written;
    COORD topLeft = {0, 0};
    FillConsoleOutputCharacter(h, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(h, topLeft);
}

/// @brief 移动光标到指定位置
/// @param x 列 (0-based)
/// @param y 行 (0-based)
void gotoxy(int x, int y) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD pos = {static_cast<SHORT>(x), static_cast<SHORT>(y)};
    SetConsoleCursorPosition(h, pos);
}

/// @brief 隐藏控制台光标（避免闪烁）
void hideCursor() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO info;
    GetConsoleCursorInfo(h, &info);
    info.bVisible = FALSE;
    SetConsoleCursorInfo(h, &info);
}

} // namespace Console

// ============================================================================
// 🖼️ 增强可视化 — 使用 Unicode 字符和 ANSI 颜色
// ============================================================================
// 根据实体能量比例用不同颜色和字符渲染：
//   >80% 绿色█(高能) / 黄色█(高代高能)
//   50-80% 青色▓
//   25-50% 橙色▒
//   <25% 红色░(濒死)
// 食物按多少用不同颜色 ▄ 或 · 表示
// ============================================================================
void renderEnhanced(DigitalLifeSimulator& sim, std::ostream& out) {
    const auto& world = sim.getWorld();
    auto& ecs = sim.getECS();

    // 顶部边框
    out << "\033[38;5;240m┌" << std::string(Config::WORLD_WIDTH * 2, '─') << "┐\033[0m\n";

    for (int y = 0; y < Config::WORLD_HEIGHT; ++y) {
        out << "\033[38;5;240m│\033[0m";
        for (int x = 0; x < Config::WORLD_WIDTH; ++x) {
            int32_t eid = world.entityGrid[y][x];
            int32_t food = world.foodGrid[y][x];

            if (eid >= 0 && ecs.isEntityAlive(eid)) {
                auto& energy = ecs.getEnergy(eid);
                auto& age = ecs.getAge(eid);
                float ratio = energy.ratio();

                if (ratio > 0.8) {
                    if (age.generation > 10)
                        out << "\033[38;5;226m█\033[0m"; // 黄色 - 高龄高能
                    else
                        out << "\033[38;5;82m█\033[0m";  // 绿色 - 高能
                } else if (ratio > 0.5) {
                    out << "\033[38;5;117m▓\033[0m";     // 青色
                } else if (ratio > 0.25) {
                    out << "\033[38;5;208m▒\033[0m";     // 橙色 - 低能
                } else {
                    out << "\033[38;5;196m░\033[0m";     // 红色 - 濒死
                }
                out << "\b"; // 退格使双宽字符正确覆盖
            } else if (food >= 8) {
                out << "\033[38;5;178m▄\033[0m";         // 丰富食物
            } else if (food >= 4) {
                out << "\033[38;5;136m▄\033[0m";         // 中等食物
            } else if (food >= 1) {
                out << "\033[38;5;240m·\033[0m";         // 少量食物
            } else {
                out << ' ';
            }
        }
        out << "\033[38;5;240m│\033[0m\n";
    }

    out << "\033[38;5;240m└" << std::string(Config::WORLD_WIDTH * 2, '─') << "┘\033[0m\n";
}

// ============================================================================
// 📟 简化 ASCII 渲染 — 兼容性更好，不依赖 ANSI 转义序列
// ============================================================================
// 字符映射：@=高能(>80%) O=中能(>50%) o=低能(>25%) .=濒死
//           #=丰富食物(≥8) :=中等食物(≥3) '=少量食物(≥1) 空格=空
// ============================================================================
void renderASCII(DigitalLifeSimulator& sim, std::ostream& out) {
    const auto& world = sim.getWorld();
    auto& ecs = sim.getECS();

    out << "+" << std::string(Config::WORLD_WIDTH, '-') << "+\n";
    for (int y = 0; y < Config::WORLD_HEIGHT; ++y) {
        out << "|";
        for (int x = 0; x < Config::WORLD_WIDTH; ++x) {
            int32_t eid = world.entityGrid[y][x];
            int32_t food = world.foodGrid[y][x];

            if (eid >= 0 && ecs.isEntityAlive(eid)) {
                auto& energy = ecs.getEnergy(eid);
                float ratio = energy.ratio();
                if (ratio > 0.8)       out << '@';
                else if (ratio > 0.5)  out << 'O';
                else if (ratio > 0.25) out << 'o';
                else                   out << '.';
            } else if (food >= 8) {
                out << '#';
            } else if (food >= 3) {
                out << ':';
            } else if (food >= 1) {
                out << '\'';
            } else {
                out << ' ';
            }
        }
        out << "|\n";
    }
    out << "+" << std::string(Config::WORLD_WIDTH, '-') << "+";
}

// ============================================================================
// 🕹️ 交互式运行 — 实时可视化 + 键盘控制
// ============================================================================
// 快捷键：
//   Space — 暂停/继续
//   Q     — 退出
//   1-5   — 速度倍率 (1x/2x/5x/10x/50x)
//   S     — 单步执行一个 tick
//   I     — 显示详细统计
// ============================================================================
void interactiveRun(DigitalLifeSimulator& sim, int delayMs = 50) {
    Console::hideCursor();

    bool paused = false;
    bool running = true;
    int speedMultiplier = 1;
    uint64_t tick = 0;

    std::cout << "\033[2J\033[H"; // 清屏

    while (running) {
        // 处理输入
        while (_kbhit()) {
            int ch = _getch();
            switch (ch) {
                case ' ':
                    paused = !paused;
                    break;
                case 'q':
                case 'Q':
                    running = false;
                    break;
                case '1': speedMultiplier = 1;  break;
                case '2': speedMultiplier = 2;  break;
                case '3': speedMultiplier = 5;  break;
                case '4': speedMultiplier = 10; break;
                case '5': speedMultiplier = 50; break;
                case 's':
                case 'S':
                    // 单步执行
                    sim.tick();
                    tick++;
                    break;
                case 'i':
                case 'I':
                    // 显示详细信息
                    paused = true;
                    Console::gotoxy(0, Config::WORLD_HEIGHT + 3);
                    sim.renderStats(std::cout);
                    break;
            }
        }

        if (!paused && running) {
            for (int i = 0; i < speedMultiplier; ++i) {
                sim.tick();
                tick++;
            }
        }

        // 渲染
        Console::gotoxy(0, 0);
        renderASCII(sim, std::cout);

        // 状态栏
        Console::gotoxy(0, Config::WORLD_HEIGHT + 2);
        std::cout << "Tick: " << std::setw(8) << std::left << sim.getWorld().tickCount
                  << "Alive: " << std::setw(4) << sim.getECS().getAliveCount()
                  << "Births: " << std::setw(6) << sim.getWorld().totalBirths
                  << "Deaths: " << std::setw(6) << sim.getWorld().totalDeaths
                  << "Mutations: " << sim.getWorld().totalMutations
                  << "    ";

        Console::gotoxy(0, Config::WORLD_HEIGHT + 3);
        std::cout << "[Space]暂停 [Q]退出 [1-5]速度x" << speedMultiplier
                  << " [S]单步 [I]详情   速度: ";

        if (paused) {
            Console::setColor(14); // 黄色
            std::cout << "⏸ 已暂停";
            Console::reset();
        } else {
            Console::setColor(10); // 绿色
            std::cout << "▶ 运行中";
            Console::reset();
        }
        std::cout << "        ";

        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    Console::gotoxy(0, Config::WORLD_HEIGHT + 5);
    sim.renderStats(std::cout);
    std::cout << "\n模拟结束。\n";
}

// ============================================================================
// 📊 批量模拟 — 无 UI，纯统计输出（适合长时间运行）
// ============================================================================
// 每隔 reportInterval 个 tick 输出一行统计：
//   Tick | Alive | Births | Deaths | Mutations | Fights | AvgEnergy | MaxGen
// ============================================================================
void batchSimulation(DigitalLifeSimulator& sim, int totalTicks, int reportInterval = 100) {
    std::cout << "===== 数字生命批量模拟 =====\n";
    std::cout << "总Tick数: " << totalTicks << "\n";
    std::cout << std::string(70, '-') << "\n";
    std::cout << std::left
              << std::setw(8) << "Tick"
              << std::setw(8) << "Alive"
              << std::setw(10) << "Births"
              << std::setw(10) << "Deaths"
              << std::setw(12) << "Mutations"
              << std::setw(10) << "Fights"
              << std::setw(12) << "AvgEnergy"
              << std::setw(10) << "MaxGen"
              << "\n";
    std::cout << std::string(70, '-') << "\n";

    auto startTime = std::chrono::steady_clock::now();

    for (int i = 0; i < totalTicks; ++i) {
        sim.tick();

        if (i % reportInterval == 0 && i > 0) {
            auto& world = sim.getWorld();
            auto& ecs = sim.getECS();

            int32_t aliveCount = ecs.getAliveCount();
            int32_t totalEnergy = 0;
            int32_t maxGen = 0;
            auto ids = ecs.getAliveEntityIds();
            for (int32_t id : ids) {
                totalEnergy += ecs.getEnergy(id).energy;
                int32_t gen = ecs.getAge(id).generation;
                if (gen > maxGen) maxGen = gen;
            }
            int32_t avgEnergy = aliveCount > 0 ? totalEnergy / aliveCount : 0;

            std::cout << std::left
                      << std::setw(8) << world.tickCount
                      << std::setw(8) << aliveCount
                      << std::setw(10) << world.totalBirths
                      << std::setw(10) << world.totalDeaths
                      << std::setw(12) << world.totalMutations
                      << std::setw(10) << world.totalFights
                      << std::setw(12) << avgEnergy
                      << std::setw(10) << maxGen
                      << "\n";
        }
    }

    auto endTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    std::cout << std::string(70, '-') << "\n";
    std::cout << "模拟完成! 耗时: " << elapsed << "ms\n";
    sim.renderStats(std::cout);
}

// ============================================================================
// 🚀 程序入口 — 三种运行模式
// ============================================================================
// [1] 交互式可视化 — 实时控制台渲染 + 键盘控制
// [2] 批量模拟 — 纯统计输出，适合长时间运行和分析
// [3] 快速演示 — 自动运行 500 ticks 并展示最终状态
// ============================================================================
int main(int argc, char* argv[]) {
    // 设置控制台 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 控制台标题
    SetConsoleTitleW(L"数字生命培养皿 - Digital Life Petri Dish");

    std::cout << R"(
╔══════════════════════════════════════════════════════╗
║         🧬 数字生命培养皿  🧬                       ║
║    Digital Life Petri Dish v1.0                      ║
║                                                      ║
║  基于 ECS 架构的虚拟机 | 极简指令集 | Tick 执行流      ║
║  数字生命: 自我复制 · 随机变异 · 能量争夺              ║
╚══════════════════════════════════════════════════════╝
)";

    // 创建模拟器
    DigitalLifeSimulator sim;

    // 初始化随机种子
    std::mt19937 rng(static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    // 初始化世界
    sim.initialize(rng);

    std::cout << "\n选择运行模式:\n";
    std::cout << "  [1] 交互式可视化 (推荐)\n";
    std::cout << "  [2] 批量模拟 (统计输出)\n";
    std::cout << "  [3] 快速演示 (自动运行 500 ticks)\n";
    std::cout << "请输入 (1/2/3): ";

    int choice = 0;
    std::cin >> choice;

    switch (choice) {
        case 1: {
            std::cout << "\n启动交互式可视化...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            interactiveRun(sim, 30);
            break;
        }
        case 2: {
            std::cout << "\n启动批量模拟...\n";
            int ticks;
            std::cout << "输入模拟 Tick 数: ";
            std::cin >> ticks;
            batchSimulation(sim, ticks, ticks / 20);
            break;
        }
        case 3:
        default: {
            std::cout << "\n快速演示模式: 500 ticks\n";
            batchSimulation(sim, 500, 50);

            // 显示最终状态和一些实体信息
            std::cout << "\n===== 最终世界状态 =====\n";
            renderASCII(sim, std::cout);
            std::cout << "\n";

            // 显示几个存活实体的详细信息
            auto aliveIds = sim.getECS().getAliveEntityIds();
            int showCount = std::min(3, static_cast<int>(aliveIds.size()));
            for (int i = 0; i < showCount; ++i) {
                sim.renderEntityInfo(std::cout, aliveIds[i]);
                std::cout << "\n";
            }
            break;
        }
    }

    return 0;
}
