// ============================================================================
// ECS 管理器 — 实现
// ============================================================================
#include "ecs_manager.h"

ECSManager::ECSManager() : m_entityCount(0), m_nextId(0) {
    // 创建三个内存池，每个池 MAX_ENTITIES 个固定大小块
    m_programPool = new MemoryPool(Config::PROGRAM_MEMORY_SIZE,        Config::MAX_ENTITIES);
    m_dataPool    = new MemoryPool(Config::DATA_MEMORY_SIZE,           Config::MAX_ENTITIES);
    m_stackPool   = new MemoryPool(Config::STACK_SIZE * sizeof(int32_t), Config::MAX_ENTITIES);
    for (int i = 0; i < Config::MAX_ENTITIES; ++i) {
        m_alive[i]       = AliveComponent();
        m_ages[i]        = AgeComponent();
        m_energies[i]    = EnergyComponent();
        m_positions[i]   = PositionComponent();
        m_energies[i].maxEnergy = Config::MAX_ENERGY;
    }
}

ECSManager::~ECSManager() { delete m_programPool; delete m_dataPool; delete m_stackPool; }

int32_t ECSManager::createEntity() {
    int32_t id;
    if (!m_freeIds.empty()) { id = m_freeIds.back(); m_freeIds.pop_back(); }
    else { if (m_nextId >= Config::MAX_ENTITIES) return Entity::INVALID; id = m_nextId++; }

    m_entities[id] = Entity(id); m_entities[id].active = true;
    m_alive[id] = AliveComponent(); m_alive[id].alive = true; m_entities[id].addComponent(COMP_ALIVE);
    m_genomes[id]  = GenomeComponent();
    m_energies[id] = EnergyComponent(); m_energies[id].energy = Config::INITIAL_ENERGY; m_energies[id].maxEnergy = Config::MAX_ENERGY;
    m_positions[id] = PositionComponent();
    m_vmStates[id] = VMState();
    m_ages[id]     = AgeComponent();
    m_sensors[id]  = SensorComponent();
    ++m_entityCount;
    return id;
}

void ECSManager::destroyEntity(int32_t eid) {
    if (eid < 0 || eid >= m_nextId || !m_entities[eid].active) return;
    if (m_vmStates[eid].programMemoryBlock >= 0) m_programPool->deallocate(m_vmStates[eid].programMemoryBlock);
    if (m_vmStates[eid].dataMemoryBlock    >= 0) m_dataPool->deallocate(m_vmStates[eid].dataMemoryBlock);
    if (m_vmStates[eid].stackMemoryBlock   >= 0) m_stackPool->deallocate(m_vmStates[eid].stackMemoryBlock);
    m_entities[eid].active = false; m_entities[eid].componentMask = 0;
    m_alive[eid].alive = false; m_freeIds.push_back(eid);
    if (m_entityCount > 0) --m_entityCount;
}

bool ECSManager::isEntityAlive(int32_t eid) const {
    return eid >= 0 && eid < m_nextId && m_entities[eid].active && m_alive[eid].alive;
}

Entity& ECSManager::getEntity(int32_t eid)       { return m_entities[eid]; }
const Entity& ECSManager::getEntity(int32_t eid) const { return m_entities[eid]; }

bool ECSManager::hasComponent(int32_t eid, ComponentType t) const {
    return eid >= 0 && eid < m_nextId && m_entities[eid].hasComponent(t);
}

GenomeComponent&   ECSManager::getGenome(int32_t eid)   { return m_genomes[eid]; }
EnergyComponent&   ECSManager::getEnergy(int32_t eid)   { return m_energies[eid]; }
PositionComponent& ECSManager::getPosition(int32_t eid) { return m_positions[eid]; }
VMState&           ECSManager::getVMState(int32_t eid)  { return m_vmStates[eid]; }
AgeComponent&      ECSManager::getAge(int32_t eid)      { return m_ages[eid]; }
AliveComponent&    ECSManager::getAlive(int32_t eid)    { return m_alive[eid]; }
SensorComponent&   ECSManager::getSensor(int32_t eid)   { return m_sensors[eid]; }

std::vector<int32_t> ECSManager::getAliveEntityIds() const {
    std::vector<int32_t> r;
    for (int32_t i = 0; i < m_nextId; ++i)
        if (m_entities[i].active && m_alive[i].alive) r.push_back(i);
    return r;
}

int32_t ECSManager::getAliveCount() const {
    int32_t c = 0;
    for (int32_t i = 0; i < m_nextId; ++i)
        if (m_entities[i].active && m_alive[i].alive) ++c;
    return c;
}
