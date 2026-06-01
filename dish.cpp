// ============================================================================
// 🧬 数字生命培养皿 (Digital Life Petri Dish) — 项目入口 & 架构说明
// ============================================================================
//
// 模块化文件结构:
// =================
// config.h           全局配置常量 (世界大小、能量、VM参数等)
//                     详见文件内详尽的 Doxygen 注释
// isa.h / isa.cpp    极简指令集架构 (35条指令, 8个通用寄存器)
//                     包括编码格式、助记符名称映射
// memory_pool.h/.cpp 内存池管理 (固定大小块分配器, LIFO 空闲链表)
//                     三个池: 程序内存 256B/实体, 数据内存 128B/实体, 栈 256B/实体
// components.h/.cpp  纯数据组件 + VM状态 + 实体定义
//                     7 种组件类型: Genome/Energy/Position/VMState/Age/Alive/Sensor
// world_state.h/.cpp 世界状态 (2D环面网格, 食物分布, 全局统计)
//                     双网格: foodGrid + entityGrid
// ecs_manager.h/.cpp ECS 管理器 (实体CRUD, 组件存取, 内存池管理)
//                     SoA 布局, O(1) 按 ID 索引访问
// rule_system.h/.cpp 法则系统 (能量守恒, 变异率, 战斗结算, 寿命)
//                     所有方法为静态纯函数
// systems.h/.cpp     所有 System (代谢→感知→VM执行→移动→进食→战斗→复制→死亡)
//                     8 个 System 按严格顺序执行
// simulator.h/.cpp   模拟器顶层 + 基因组生成 + 可视化渲染
//                     种子基因组包含硬编码的初始生存策略
//
// 整体版（单文件）:
// =================
// digital_life_vm.h   整体版头文件 — 包含所有模块定义
// digital_life_vm.cpp 整体版实现 — 包含所有模块代码
//
// 演示入口: main_digital_life.cpp
//   三种模式: [1]交互式可视化 [2]批量模拟 [3]快速演示
//
// 编译 (MSVC):
//   cl /utf-8 /std:c++17 /EHsc /MD /O2 ^
//      main_digital_life.cpp simulator.cpp systems.cpp ecs_manager.cpp ^
//      rule_system.cpp world_state.cpp components.cpp memory_pool.cpp isa.cpp ^
//      /Fe:digital_life.exe
//
// 核心概念:
// ==========
//   🧬 基因组 = 程序代码 — 每个数字生命由一段字节码程序定义
//   ⚡ 能量 = 经济货币 — 所有行为都消耗能量，能量≤0则死亡
//   🔄 自我复制 — 实体执行 REPLICATE 指令产生基因变异的子代
//   🎲 随机变异 — 复制时发生点突变/插入/删除，驱动进化
//   ⚔️ 能量争夺 — 实体可通过 FIGHT 指令夺取其他实体的能量
//   🌍 环面世界 — 64x64 网格，上下左右边界无缝连接
//   🍎 随机食物 — 每 tick 在随机位置生成食物，模拟环境资源
//   💀 自然死亡 — 能量耗尽或年龄超标则死亡，能量转化为食物
// ============================================================================

