// ============================================================================
// 🎮 数字生命模拟器 + 基因组生成 + 可视化 — 实现
// ============================================================================
#include "simulator.h"
#include "isa.h"  // for ISA::MOV, ISA::SENSE etc. in genome generation
#include <chrono>
#include <map>

// ============================================================================
// DigitalLifeSimulator — 构造与初始化
// ============================================================================

DigitalLifeSimulator::DigitalLifeSimulator()
    : m_rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()))
{
    setupSystems();  // 按顺序注册 8 个 System
}

DigitalLifeSimulator::~DigitalLifeSimulator() {
    for (auto* s : m_systems) delete s;
}

// 注册 System 管线（按 Tick 执行顺序）
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

// 初始化世界：生成食物 + 在世界中心放置 8 个种子生命
// 每个种子生命使用相同的 SeedGenome，但经过一轮变异产生初始多样性
void DigitalLifeSimulator::initialize(std::mt19937& rng) {
    m_rng = rng; m_world.reset();
    for (int i = 0; i < 100; ++i) m_world.spawnFood(m_rng);
    int32_t cx = Config::WORLD_WIDTH / 2, cy = Config::WORLD_HEIGHT / 2;
    for (int i = 0; i < 8; ++i) {
        auto genome = generateSeedGenome(m_rng);
        RuleSystem::applyMutations(genome, m_rng, 0, 100);
        spawnPrimordialLife(m_rng, cx + (i % 3) - 1, cy + (i / 3) - 1, genome);
    }
}

// 执行单个 Tick：食物生成 → 年龄增长 → 按管线顺序执行所有 System
void DigitalLifeSimulator::tick() {
    m_world.tickCount++; m_world.spawnFood(m_rng);
    m_ecs.forEachAlive([&](int32_t id, Entity&) { m_ecs.getAge(id).age++; });
    for (auto* sys : m_systems) sys->execute(m_ecs, m_world, m_rng);
}

void DigitalLifeSimulator::run(int32_t n, bool verbose) {
    for (int32_t i = 0; i < n; ++i) {
        tick();
        if (verbose && i % 10 == 0)
            std::cout << "Tick " << m_world.tickCount << " | Alive: " << m_ecs.getAliveCount()
                      << " | Births: " << m_world.totalBirths << " | Deaths: " << m_world.totalDeaths << "\n";
    }
}

// 创建原始生命：分配实体 → 分配内存池 → 加载基因组 → 放置到世界
int32_t DigitalLifeSimulator::spawnPrimordialLife(std::mt19937& rng, int32_t x, int32_t y,
                                                    const std::vector<uint8_t>& genome) {
    int32_t eid = m_ecs.createEntity(); if (eid < 0) return -1;
    auto& vm = m_ecs.getVMState(eid);
    vm.programMemoryBlock = m_ecs.getProgramMemory().allocate();
    vm.dataMemoryBlock    = m_ecs.getDataMemory().allocate();
    vm.stackMemoryBlock   = m_ecs.getStackMemory().allocate();
    auto& gc = m_ecs.getGenome(eid); gc.code = genome;
    gc.checksum = gc.computeChecksum(); gc.parentChecksum = 0;
    vm.loadGenome(genome);
    m_ecs.getEnergy(eid).energy = Config::INITIAL_ENERGY;
    m_ecs.getEnergy(eid).maxEnergy = Config::MAX_ENERGY;
    auto& pos = m_ecs.getPosition(eid);
    pos.x = DL_WRAP_COORD(x, Config::WORLD_WIDTH); pos.y = DL_WRAP_COORD(y, Config::WORLD_HEIGHT);
    if (m_world.getEntity(pos.x, pos.y) >= 0)
        { pos.x = std::uniform_int_distribution<int32_t>(0,Config::WORLD_WIDTH-1)(rng);
          pos.y = std::uniform_int_distribution<int32_t>(0,Config::WORLD_HEIGHT-1)(rng); }
    m_world.setEntity(pos.x, pos.y, eid);
    m_ecs.getAge(eid).parentId = -1;
    auto& ent = m_ecs.getEntity(eid);
    ent.addComponent(COMP_GENOME); ent.addComponent(COMP_ENERGY); ent.addComponent(COMP_POSITION);
    ent.addComponent(COMP_VMSTATE); ent.addComponent(COMP_AGE); ent.addComponent(COMP_SENSOR);
    return eid;
}

// ============================================================================
// 🧬 基因组生成
// ============================================================================

// 生成完全随机的基因组（每个字节 0-255 均匀随机）
std::vector<uint8_t> DigitalLifeSimulator::generateRandomGenome(std::mt19937& rng, size_t len) {
    std::vector<uint8_t> g(len);
    for (auto& b : g) b = static_cast<uint8_t>(std::uniform_int_distribution<int32_t>(0, 255)(rng));
    return g;
}

// 生成种子基因组——硬编码的初始生存策略
// 策略循环：SENSE → 低能→进食 → 中能→战斗 → 高能→复制 → 随机移动
// 这个简单行为是所有进化的起点，变异会产生更复杂的策略
std::vector<uint8_t> DigitalLifeSimulator::generateSeedGenome(std::mt19937& rng) {
    std::vector<uint8_t> g;
    auto addMovImm=[&](uint8_t rd,int32_t imm){g.push_back(ISA::MOV);g.push_back(rd&7);g.push_back(0x10);
        if(imm<128)g.push_back((uint8_t)imm);else{g.push_back(0x80|(uint8_t)(imm&0x7F));g.push_back((uint8_t)((imm>>7)&0x7F));}};
    auto addMovReg=[&](uint8_t rd,uint8_t rs){g.push_back(ISA::MOV);g.push_back(rd&7);g.push_back(rs&7);};
    auto addCmp=[&](uint8_t rd,uint8_t rs){g.push_back(ISA::CMP);g.push_back(rd&7);g.push_back(rs&7);};
    auto addJmp=[&](ISA::Opcode jt,uint8_t addr){g.push_back(jt);g.push_back(addr);};

    uint8_t loop=(uint8_t)g.size(); g.push_back(ISA::SENSE); g.push_back(ISA::EAT);
    addMovImm(5,2); addCmp(1,5); addJmp(ISA::JL,0); uint8_t fjp=(uint8_t)g.size()-1;
    addMovImm(5,80); addCmp(6,5); addJmp(ISA::JG,0); uint8_t rjp=(uint8_t)g.size()-1;
    addMovImm(0,(int32_t)(rng()%4)); g.push_back(ISA::MOVE); addJmp(ISA::JMP,loop);

    uint8_t fs=(uint8_t)g.size(); g[fjp]=fs;
    addMovReg(1,3); g.push_back(ISA::FIGHT); addJmp(ISA::JMP,loop);

    uint8_t rs=(uint8_t)g.size(); g[rjp]=rs;
    g.push_back(ISA::REPLICATE); addJmp(ISA::JMP,loop);
    return g;
}

// ============================================================================
// 🖼️ 可视化
// ============================================================================

// ASCII 渲染：@=高能(>80%) O=中能(>50%) o=低能(>25%) .=濒死 #=丰富食物 :=中等食物 '=少量食物
void DigitalLifeSimulator::render(std::ostream& out) {
    out << "+" << std::string(Config::WORLD_WIDTH, '-') << "+\n";
    for (int y=0;y<Config::WORLD_HEIGHT;++y){ out<<"|";
        for (int x=0;x<Config::WORLD_WIDTH;++x){
            int32_t eid=m_world.entityGrid[y][x],f=m_world.foodGrid[y][x];
            if(eid>=0&&m_ecs.isEntityAlive(eid)){float r=m_ecs.getEnergy(eid).ratio();
                out<<(r>0.8?'@':r>0.5?'O':r>0.25?'o':'.');}
            else if(f>=8)out<<'#';else if(f>=3)out<<':';else if(f>=1)out<<'\'';else out<<' ';
        } out<<"|\n";
    } out<<"+"<<std::string(Config::WORLD_WIDTH,'-')<<"+";
}

void DigitalLifeSimulator::renderDetailed(std::ostream& out) { render(out); out<<"\n"; renderStats(out); }

void DigitalLifeSimulator::renderStats(std::ostream& out) {
    int32_t ac=m_ecs.getAliveCount(); auto ids=m_ecs.getAliveEntityIds();
    std::map<int32_t,int32_t> gc; int32_t te=0,mg=0,tgs=0;
    for(int32_t id:ids){auto&a=m_ecs.getAge(id);auto&e=m_ecs.getEnergy(id);gc[a.generation]++;te+=e.energy;
        if(a.generation>mg)mg=a.generation;tgs+=static_cast<int32_t>(m_ecs.getGenome(id).code.size());}
    out<<"====== 数字生命培养皿 统计 ======\n";
    out<<"Tick: "<<m_world.tickCount<<" | Alive: "<<ac<<" | Births: "<<m_world.totalBirths
       <<" | Deaths: "<<m_world.totalDeaths<<"\n";
    out<<"复制: "<<m_world.totalReplications<<" | 战斗: "<<m_world.totalFights
       <<" | 变异: "<<m_world.totalMutations<<"\n";
    out<<"食物生成: "<<m_world.totalFoodSpawned<<" | 食物消耗: "<<m_world.totalFoodConsumed<<"\n";
    if(ac>0)out<<"平均能量: "<<te/ac<<" | 最大代数: G"<<mg<<" | 平均基因组: "<<tgs/ac<<"B\n";
    out<<"代数: "; for(auto&[g,c]:gc)out<<"G"<<g<<":"<<c<<" "; out<<"\n";
}

void DigitalLifeSimulator::renderEntityInfo(std::ostream& out, int32_t eid) {
    if(!m_ecs.isEntityAlive(eid)){out<<"实体 "<<eid<<" 已死亡\n";return;}
    auto& age=m_ecs.getAge(eid);auto& en=m_ecs.getEnergy(eid);auto& pos=m_ecs.getPosition(eid);
    auto& vm=m_ecs.getVMState(eid);auto& gn=m_ecs.getGenome(eid);auto& sn=m_ecs.getSensor(eid);
    out<<"===== 实体 #"<<eid<<" =====\n";
    out<<"代数: G"<<age.generation<<" | 年龄: "<<age.age<<" | 父: "<<age.parentId<<"\n";
    out<<"能量: "<<en.energy<<"/"<<en.maxEnergy<<" | 位置: ("<<pos.x<<","<<pos.y<<")\n";
    out<<"基因组: "<<gn.code.size()<<"B | CRC: 0x"<<std::hex<<gn.checksum<<std::dec<<"\n";
    out<<vm.disassemble()<<"\n";
    out<<"传感器: 最近E"<<sn.nearestEntityId<<" d="<<sn.nearestDistance<<" e="<<sn.nearestEnergy
       <<" 食物="<<sn.foodAtPosition<<" 附近="<<sn.entityCountNearby<<"\n";
}
