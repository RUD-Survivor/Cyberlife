// ============================================================================
// 🔄 ECS System 管线 — 完整实现
// ============================================================================
// 每个 System::execute() 接收 ECS 管理器、世界状态和随机数生成器。
// 系统按以下顺序执行：Metabolism → Sense → VMExecute → Movement →
// Eating → Combat → Replication → Death
// ============================================================================
#include "systems.h"
#include "rule_system.h"
#include "isa.h"
#include <algorithm>

// ============================================================================
// 1️⃣ MetabolismSystem — 能量衰减 + 能量耗尽即死
// ============================================================================
void MetabolismSystem::execute(ECSManager& ecs, WorldState& world, std::mt19937&) {
    for (int32_t id : ecs.getAliveEntityIds()) {
        auto& energy = ecs.getEnergy(id);
        auto& age    = ecs.getAge(id);
        int32_t decay = RuleSystem::calculateDecay(energy.energy, age.age);
        energy.spend(decay);
        if (energy.isDead()) {
            auto& alive = ecs.getAlive(id);
            alive.alive = false; alive.deathTick = static_cast<int32_t>(world.tickCount);
            alive.deathCause = "energy_depleted"; world.totalDeaths++;
            auto& pos = ecs.getPosition(id);
            world.addFood(pos.x, pos.y, std::max(0, energy.energy + decay));
            world.clearEntity(pos.x, pos.y);
        }
    }
}

// ============================================================================
// 2️⃣ SenseSystem — 环境感知（填充传感器缓存）
// ============================================================================
void SenseSystem::execute(ECSManager& ecs, WorldState& world, std::mt19937&) {
    auto ids = ecs.getAliveEntityIds();
    for (int32_t id : ids) {
        auto& sensor = ecs.getSensor(id);
        auto& pos    = ecs.getPosition(id);
        sensor = SensorComponent(); // reset
        sensor.nearestDistance = Config::SENSE_RANGE + 1;
        sensor.foodAtPosition = world.getFood(pos.x, pos.y);

        int32_t totalE = 0;
        for (int32_t oid : ids) {
            if (oid == id) continue;
            auto& op = ecs.getPosition(oid);
            int32_t d = pos.distanceTo(op);
            if (d <= Config::SENSE_RANGE) {
                sensor.entityCountNearby++; totalE += ecs.getEnergy(oid).energy;
                if (d < sensor.nearestDistance) {
                    sensor.nearestDistance = d; sensor.nearestEntityId = oid;
                    sensor.nearestEnergy = ecs.getEnergy(oid).energy;
                    sensor.nearestDirection = pos.directionTo(op);
                }
            }
        }
        if (sensor.entityCountNearby > 0) sensor.avgEnergyNearby = totalE / sensor.entityCountNearby;
    }
}

// ============================================================================
// 3️⃣ VMExecutionSystem — 虚拟机取指、解码、执行
// ============================================================================

// 取指：从 genome[IP] 读取操作码
uint8_t VMExecutionSystem::fetchOpcode(VMState& vm) {
    if (vm.ip >= static_cast<uint16_t>(vm.genome.size())) return ISA::HALT;
    return vm.genome[vm.ip];
}

// 读取可变长立即数（1-2 字节）
// 如果第 7 位为 1，则继续读取下一个字节的低 7 位拼接
int32_t VMExecutionSystem::fetchImmediate(VMState& vm) {
    uint16_t nip = vm.ip;
    if (nip >= static_cast<uint16_t>(vm.genome.size())) return 0;
    int32_t val = static_cast<int32_t>(vm.genome[nip]); vm.ip = nip;
    if (val & 0x80) {
        val &= 0x7F; nip = vm.ip + 1;
        if (nip < static_cast<uint16_t>(vm.genome.size()))
            { val = (val << 7) | static_cast<int32_t>(vm.genome[nip] & 0x7F); vm.ip = nip; }
    }
    return val;
}

// 执行单条指令的主分发函数
// 返回 true=继续执行, false=实体 halted 或死亡
bool VMExecutionSystem::executeInstruction(ECSManager& ecs, WorldState& world,
                                            int32_t eid, std::mt19937& rng) {
    auto& vm     = ecs.getVMState(eid);
    auto& sensor = ecs.getSensor(eid);
    auto& energy = ecs.getEnergy(eid);
    auto& pos    = ecs.getPosition(eid);
    auto& age    = ecs.getAge(eid);

    if (vm.halted || vm.ip >= static_cast<uint16_t>(vm.genome.size())) { vm.halted = true; return false; }

    uint8_t opcode = fetchOpcode(vm); vm.ip++;

    auto readReg = [&]() -> uint8_t {
        if (vm.ip >= static_cast<uint16_t>(vm.genome.size())) return 0;
        return vm.genome[vm.ip++] & 0x07;
    };
    auto readImm = [&]() -> int32_t { return fetchImmediate(vm); };

    switch (opcode) {
    case ISA::NOP: break;
    case ISA::MOV: {
        uint8_t rd = readReg();
        uint16_t nip = vm.ip;
        uint8_t mode = (nip < static_cast<uint16_t>(vm.genome.size())) ? vm.genome[nip] : 0; vm.ip = nip + 1;
        if (mode & 0x10) { int32_t imm = readImm(); if (rd < Config::NUM_REGISTERS) vm.regs[rd] = imm; }
        else { uint8_t rs = mode & 0x07; if (rd < Config::NUM_REGISTERS && rs < Config::NUM_REGISTERS) vm.regs[rd] = vm.regs[rs]; }
        break;
    }
    case ISA::ADD: { uint8_t rd = readReg(), rs = readReg(); if (rd < 8 && rs < 8) vm.regs[rd] += vm.regs[rs]; break; }
    case ISA::SUB: { uint8_t rd = readReg(), rs = readReg(); if (rd < 8 && rs < 8) vm.regs[rd] -= vm.regs[rs]; break; }
    case ISA::MUL: { uint8_t rd = readReg(), rs = readReg(); if (rd < 8 && rs < 8) vm.regs[rd] *= vm.regs[rs]; break; }
    case ISA::DIV: { uint8_t rd = readReg(), rs = readReg(); if (rd < 8 && rs < 8 && vm.regs[rs]) vm.regs[rd] /= vm.regs[rs]; break; }
    case ISA::INC: { uint8_t rd = readReg(); if (rd < 8) vm.regs[rd]++; break; }
    case ISA::DEC: { uint8_t rd = readReg(); if (rd < 8) vm.regs[rd]--; break; }
    case ISA::AND_OP: { uint8_t rd = readReg(), rs = readReg(); if (rd < 8 && rs < 8) vm.regs[rd] &= vm.regs[rs]; break; }
    case ISA::OR_OP:  { uint8_t rd = readReg(), rs = readReg(); if (rd < 8 && rs < 8) vm.regs[rd] |= vm.regs[rs]; break; }
    case ISA::XOR_OP: { uint8_t rd = readReg(), rs = readReg(); if (rd < 8 && rs < 8) vm.regs[rd] ^= vm.regs[rs]; break; }
    case ISA::NOT_OP: { uint8_t rd = readReg(); if (rd < 8) vm.regs[rd] = ~vm.regs[rd]; break; }
    case ISA::SHL: { uint8_t rd = readReg(); int32_t imm = readImm() & 0x1F; if (rd < 8) vm.regs[rd] <<= imm; break; }
    case ISA::SHR: { uint8_t rd = readReg(); int32_t imm = readImm() & 0x1F; if (rd < 8) vm.regs[rd] >>= imm; break; }
    case ISA::CMP: {
        uint8_t rd = readReg(), rs = readReg();
        if (rd < 8 && rs < 8) { int32_t r = vm.regs[rd] - vm.regs[rs]; vm.flags = 0;
            if (r == 0) vm.flags |= ISA::FLAG_ZERO; if (r < 0) vm.flags |= ISA::FLAG_NEGATIVE; }
        break;
    }
    case ISA::JMP: vm.ip = static_cast<uint16_t>(readImm() % Config::PROGRAM_MEMORY_SIZE); break;
    case ISA::JE:  { int32_t a = readImm(); if (vm.flags & ISA::FLAG_ZERO) vm.ip = static_cast<uint16_t>(a % Config::PROGRAM_MEMORY_SIZE); break; }
    case ISA::JNE: { int32_t a = readImm(); if (!(vm.flags & ISA::FLAG_ZERO)) vm.ip = static_cast<uint16_t>(a % Config::PROGRAM_MEMORY_SIZE); break; }
    case ISA::JG:  { int32_t a = readImm(); if (!(vm.flags & (ISA::FLAG_ZERO | ISA::FLAG_NEGATIVE))) vm.ip = static_cast<uint16_t>(a % Config::PROGRAM_MEMORY_SIZE); break; }
    case ISA::JL:  { int32_t a = readImm(); if (vm.flags & ISA::FLAG_NEGATIVE) vm.ip = static_cast<uint16_t>(a % Config::PROGRAM_MEMORY_SIZE); break; }
    case ISA::JGE: { int32_t a = readImm(); if (!(vm.flags & ISA::FLAG_NEGATIVE)) vm.ip = static_cast<uint16_t>(a % Config::PROGRAM_MEMORY_SIZE); break; }
    case ISA::JLE: { int32_t a = readImm(); if (vm.flags & (ISA::FLAG_ZERO | ISA::FLAG_NEGATIVE)) vm.ip = static_cast<uint16_t>(a % Config::PROGRAM_MEMORY_SIZE); break; }
    case ISA::PUSH: {
        uint8_t rs = readReg(); if (rs < 8 && vm.sp < Config::STACK_SIZE)
            if (auto* s = ecs.getStackMemory().getBlock(vm.stackMemoryBlock))
                { reinterpret_cast<int32_t*>(s)[vm.sp++] = vm.regs[rs]; }
        break;
    }
    case ISA::POP: {
        uint8_t rd = readReg(); if (rd < 8 && vm.sp > 0)
            if (auto* s = ecs.getStackMemory().getBlock(vm.stackMemoryBlock))
                { vm.regs[rd] = reinterpret_cast<int32_t*>(s)[--vm.sp]; }
        break;
    }
    case ISA::CALL: {
        int32_t addr = readImm();
        if (vm.sp < Config::STACK_SIZE)
            if (auto* s = ecs.getStackMemory().getBlock(vm.stackMemoryBlock))
                { reinterpret_cast<int32_t*>(s)[vm.sp++] = static_cast<int32_t>(vm.ip); }
        vm.ip = static_cast<uint16_t>(addr % Config::PROGRAM_MEMORY_SIZE); break;
    }
    case ISA::RET: {
        if (vm.sp > 0) if (auto* s = ecs.getStackMemory().getBlock(vm.stackMemoryBlock))
            vm.ip = static_cast<uint16_t>(reinterpret_cast<int32_t*>(s)[--vm.sp] % Config::PROGRAM_MEMORY_SIZE);
        break;
    }
    case ISA::LOAD: {
        uint8_t rd = readReg(); int32_t addr = readImm() % (Config::DATA_MEMORY_SIZE / 4);
        if (rd < 8) if (auto* d = ecs.getDataMemory().getBlock(vm.dataMemoryBlock))
            vm.regs[rd] = reinterpret_cast<int32_t*>(d)[addr];
        break;
    }
    case ISA::STORE: {
        int32_t addr = readImm() % (Config::DATA_MEMORY_SIZE / 4); uint8_t rs = readReg();
        if (rs < 8) if (auto* d = ecs.getDataMemory().getBlock(vm.dataMemoryBlock))
            reinterpret_cast<int32_t*>(d)[addr] = vm.regs[rs];
        break;
    }
    case ISA::REPLICATE: {
        energy.spend(Config::REPLICATION_COST / 2);
        vm.regs[0] = ReplicationSystem::replicateEntity(ecs, world, eid, rng);
        world.totalReplications++; break;
    }
    case ISA::EAT: { vm.regs[0] = EatingSystem::eatAtPosition(ecs, world, eid) ? 1 : 0; break; }
    case ISA::FIGHT: {
        int32_t d = DL_CLAMP(vm.regs[1], 0, 3), tx = pos.x, ty = pos.y;
        switch (d) { case 0: ty=DL_WRAP_COORD(ty-1,Config::WORLD_HEIGHT);break; case 1: tx=DL_WRAP_COORD(tx+1,Config::WORLD_WIDTH);break;
                     case 2: ty=DL_WRAP_COORD(ty+1,Config::WORLD_HEIGHT);break; case 3: tx=DL_WRAP_COORD(tx-1,Config::WORLD_WIDTH);break; }
        int32_t tid = world.getEntity(tx, ty); bool ok = false;
        if (tid >= 0 && tid != eid) ok = CombatSystem::fightEntity(ecs, world, eid, tid);
        vm.regs[0] = ok ? 1 : 0; world.totalFights++; break;
    }
    case ISA::SENSE: {
        energy.spend(Config::SENSE_ENERGY_COST);
        vm.regs[0]=sensor.nearestEntityId; vm.regs[1]=sensor.nearestDistance;
        vm.regs[2]=sensor.nearestEnergy;   vm.regs[3]=sensor.nearestDirection;
        vm.regs[4]=sensor.foodAtPosition;  vm.regs[5]=sensor.entityCountNearby;
        vm.regs[6]=energy.energy;          vm.regs[7]=age.age;
        break;
    }
    case ISA::MOVE: {
        int32_t d = DL_CLAMP(vm.regs[0], 0, 3);
        vm.regs[0] = MovementSystem::moveEntity(ecs, world, eid, d) ? 1 : 0; break;
    }
    case ISA::DIE: {
        auto& al = ecs.getAlive(eid); al.alive = false;
        al.deathTick = static_cast<int32_t>(world.tickCount); al.deathCause = "self_termination";
        vm.halted = true; world.clearEntity(pos.x, pos.y);
        world.addFood(pos.x, pos.y, energy.energy); energy.energy = 0; world.totalDeaths++;
        return false;
    }
    case ISA::HALT: vm.halted = true; return false;
    default: break; // unknown -> NOP
    }
    if (vm.ip >= Config::PROGRAM_MEMORY_SIZE) vm.ip = 0;
    return !vm.halted;
}

// 每 tick 为每个存活实体执行 TICKS_PER_STEP 条指令
void VMExecutionSystem::execute(ECSManager& ecs, WorldState& world, std::mt19937& rng) {
    for (int32_t id : ecs.getAliveEntityIds()) {
        auto& vm = ecs.getVMState(id); auto& energy = ecs.getEnergy(id);
        energy.energySpentThisTick = 0;
        for (int i = 0; i < Config::TICKS_PER_STEP && !vm.halted && !energy.isDead(); ++i)
            if (!executeInstruction(ecs, world, id, rng)) break;
        if (energy.isDead()) vm.halted = true;
    }
}

// ============================================================================
// 4️⃣ MovementSystem — 实体移动 + 网格一致性重建
// ============================================================================

// 由 VM MOVE 指令调用：向指定方向移动一格
bool MovementSystem::moveEntity(ECSManager& ecs, WorldState& world, int32_t eid, int32_t dir) {
    auto& energy = ecs.getEnergy(eid);
    if (!RuleSystem::canMove(energy.energy)) return false;
    auto& pos = ecs.getPosition(eid); int32_t ox = pos.x, oy = pos.y;
    pos.move(dir);
    int32_t occ = world.getEntity(pos.x, pos.y);
    if (occ >= 0 && occ != eid) { pos.x = ox; pos.y = oy; return false; }
    world.clearEntity(ox, oy); world.setEntity(pos.x, pos.y, eid);
    energy.spend(Config::MOVE_ENERGY_COST); return true;
}

// 每 tick 重建 entityGrid：从 Position 组件反向填充
void MovementSystem::execute(ECSManager& ecs, WorldState& world, std::mt19937& rng) {
    for (int y=0;y<Config::WORLD_HEIGHT;++y)for(int x=0;x<Config::WORLD_WIDTH;++x)world.entityGrid[y][x]=-1;
    for (int32_t id : ecs.getAliveEntityIds()) {
        auto& pos = ecs.getPosition(id);
        if (world.entityGrid[pos.y][pos.x] >= 0) {
            pos.x = std::uniform_int_distribution<int32_t>(0,Config::WORLD_WIDTH-1)(rng);
            pos.y = std::uniform_int_distribution<int32_t>(0,Config::WORLD_HEIGHT-1)(rng);
        }
        world.entityGrid[pos.y][pos.x] = id;
    }
}

// ============================================================================
// 5️⃣ EatingSystem — 消耗食物获取能量（由 VM EAT 指令驱动）
// ============================================================================
bool EatingSystem::eatAtPosition(ECSManager& ecs, WorldState& world, int32_t eid) {
    auto& energy = ecs.getEnergy(eid); auto& pos = ecs.getPosition(eid);
    int32_t food = world.getFood(pos.x, pos.y);
    if (food <= 0) return false;
    int32_t gained = RuleSystem::calculateEnergyGainFromFood(food);
    world.consumeFood(pos.x, pos.y, gained); energy.gain(gained); return true;
}
void EatingSystem::execute(ECSManager&, WorldState&, std::mt19937&) {}

// ============================================================================
// 6️⃣ CombatSystem — 实体间能量争夺（由 VM FIGHT 指令驱动）
// ============================================================================
bool CombatSystem::fightEntity(ECSManager& ecs, WorldState& world, int32_t atk, int32_t def) {
    if (!ecs.isEntityAlive(def)) return false;
    auto& ae = ecs.getEnergy(atk); auto& de = ecs.getEnergy(def);
    if (!RuleSystem::canFight(ae.energy)) return false;
    ae.spend(Config::FIGHT_ENERGY_COST);
    int32_t transfer; bool atkWins;
    RuleSystem::fightResolve(ae.energy, de.energy, transfer, atkWins);
    if (atkWins) { de.spend(transfer); ae.gain(transfer); }
    else         { ae.spend(transfer/2); }
    if (de.isDead()) {
        auto& dal = ecs.getAlive(def); dal.alive = false;
        dal.deathTick = static_cast<int32_t>(world.tickCount); dal.deathCause = "killed_in_combat";
        auto& dp = ecs.getPosition(def); world.clearEntity(dp.x, dp.y);
        world.addFood(dp.x, dp.y, std::max(0, de.energy)); de.energy = 0; world.totalDeaths++;
    }
    return true;
}
void CombatSystem::execute(ECSManager&, WorldState&, std::mt19937&) {}

// ============================================================================
// 7️⃣ ReplicationSystem — 自我复制 + 基因变异（由 VM REPLICATE 指令驱动）
// ============================================================================
int32_t ReplicationSystem::replicateEntity(ECSManager& ecs, WorldState& world,
                                            int32_t pid, std::mt19937& rng) {
    auto& pe = ecs.getEnergy(pid); auto& pa = ecs.getAge(pid); auto& pp = ecs.getPosition(pid);
    if (!RuleSystem::canReplicate(pe.energy, pa.age, pa.generation, ecs.getSensor(pid).entityCountNearby))
        return -1;
    int32_t cost = RuleSystem::calculateReplicationCost(pe.energy);
    if (pe.energy < cost) return -1; pe.spend(cost);

    int32_t cid = ecs.createEntity();
    if (cid < 0) { pe.gain(cost); return -1; }

    auto& cv = ecs.getVMState(cid);
    cv.programMemoryBlock=ecs.getProgramMemory().allocate();
    cv.dataMemoryBlock=ecs.getDataMemory().allocate();
    cv.stackMemoryBlock=ecs.getStackMemory().allocate();

    auto& cg = ecs.getGenome(cid); auto& pg = ecs.getGenome(pid);
    cg.code = pg.code; std::vector<uint8_t> mg = cg.code;
    RuleSystem::applyMutations(mg, rng, pa.generation + 1, pe.energy);
    if (mg != cg.code) world.totalMutations++;
    cg.code = mg; cg.parentChecksum = pg.computeChecksum(); cg.checksum = cg.computeChecksum();
    cv.loadGenome(cg.code);

    if (auto* sd = ecs.getDataMemory().getBlock(ecs.getVMState(pid).dataMemoryBlock))
        if (auto* dd = ecs.getDataMemory().getBlock(cv.dataMemoryBlock))
            std::memcpy(dd, sd, Config::DATA_MEMORY_SIZE);

    ecs.getEnergy(cid).energy = Config::INITIAL_ENERGY / 2;
    ecs.getEnergy(cid).maxEnergy = Config::MAX_ENERGY;

    int32_t off[4][2]={{0,-1},{1,0},{0,1},{-1,0}}; auto& cp=ecs.getPosition(cid); bool placed=false;
    for (auto& o : off) {
        int32_t nx=DL_WRAP_COORD(pp.x+o[0],Config::WORLD_WIDTH),ny=DL_WRAP_COORD(pp.y+o[1],Config::WORLD_HEIGHT);
        if (world.getEntity(nx,ny)<0) { cp.x=nx;cp.y=ny;world.setEntity(nx,ny,cid);placed=true;break; }
    }
    if (!placed) { cp.x=std::uniform_int_distribution<int32_t>(0,Config::WORLD_WIDTH-1)(rng);
                   cp.y=std::uniform_int_distribution<int32_t>(0,Config::WORLD_HEIGHT-1)(rng);
                   world.setEntity(cp.x,cp.y,cid); }

    auto& ca = ecs.getAge(cid); ca.age=0; ca.generation=pa.generation+1; ca.parentId=pid;
    auto& ce = ecs.getEntity(cid);
    ce.addComponent(COMP_GENOME); ce.addComponent(COMP_ENERGY); ce.addComponent(COMP_POSITION);
    ce.addComponent(COMP_VMSTATE); ce.addComponent(COMP_AGE); ce.addComponent(COMP_SENSOR);
    world.totalBirths++; return cid;
}
void ReplicationSystem::execute(ECSManager&, WorldState&, std::mt19937&) {}

// ============================================================================
// 8️⃣ DeathSystem — 老死判定 + 能量耗尽清理 + 实体销毁
// ============================================================================
void DeathSystem::execute(ECSManager& ecs, WorldState& world, std::mt19937&) {
    for (int32_t id : ecs.getAliveEntityIds()) {
        auto& al=ecs.getAlive(id); auto& ag=ecs.getAge(id);
        auto& en=ecs.getEnergy(id); auto& po=ecs.getPosition(id);
        if (RuleSystem::shouldDieOfAge(ag.age, ag.generation)) {
            al.alive=false; al.deathTick=static_cast<int32_t>(world.tickCount); al.deathCause="old_age";
            world.clearEntity(po.x,po.y); world.addFood(po.x,po.y,std::max(0,en.energy));
            en.energy=0; world.totalDeaths++;
        } else if (en.isDead() && al.alive) {
            al.alive=false; al.deathTick=static_cast<int32_t>(world.tickCount); al.deathCause="energy_depleted";
            world.clearEntity(po.x,po.y); world.totalDeaths++;
        }
    }
    for (int32_t i=0;i<Config::MAX_ENTITIES;++i)
        if (ecs.getEntity(i).active && !ecs.getAlive(i).alive) ecs.destroyEntity(i);
}
