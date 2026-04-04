/*
    addphysics.cpp
    Author: MrZer0
    EncoreMP — C++ port of OpenMWLuaPhysics (original by MaxYari)
    Физика предметов + перетаскивание (E зажать).
*/
#include "addphysics.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwphysics/collisiontype.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"

namespace MWMechanics
{

std::string AddPhysicsSystem::makeKey(const std::string& refId,
                                       const std::string& cellId)
{
    return refId + "@" + cellId;
}

void AddPhysicsSystem::registerObject(const MWWorld::Ptr& ptr,
                                       const osg::Vec3f& initialVelocity)
{
    if (ptr.isEmpty()) return;

    const std::string refId  = ptr.getCellRef().getRefId();
    const std::string cellId = ptr.getCell()->getCell()->getDescription();
    const std::string key    = makeKey(refId, cellId);

    if (mObjects.count(key)) {
        // уже зарегистрирован — если дан импульс, разбудим объект
        if (initialVelocity.length() > 0.1f) {
            auto& obj = mObjects[key];
            obj.velocity  += initialVelocity;
            obj.sleeping   = false;
            obj.timeAsleep = 0.f;
        }
        return;
    }

    PhysicsObject obj;
    obj.refId    = refId;
    obj.cellId   = cellId;
    obj.position = ptr.getRefData().getPosition().asVec3();
    obj.velocity = initialVelocity;

    if (ptr.getClass().hasToolTip(ptr))
    {
        float w = ptr.getClass().getWeight(ptr);
        obj.weight = std::max(0.1f, w);
    }

    obj.sleeping = (initialVelocity.length() < obj.sleepThreshold);
    mObjects[key] = obj;
}

void AddPhysicsSystem::removeObject(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty()) return;
    const std::string key = makeKey(ptr.getCellRef().getRefId(),
                                     ptr.getCell()->getCell()->getDescription());
    if (mDragKey == key) mDragKey.clear();
    mObjects.erase(key);
}

void AddPhysicsSystem::applyImpulse(const MWWorld::Ptr& ptr,
                                     const osg::Vec3f& impulse)
{
    if (ptr.isEmpty()) return;
    const std::string key = makeKey(ptr.getCellRef().getRefId(),
                                     ptr.getCell()->getCell()->getDescription());
    auto it = mObjects.find(key);
    if (it == mObjects.end()) return;
    it->second.velocity  += impulse;
    it->second.sleeping   = false;
    it->second.timeAsleep = 0.f;
}

void AddPhysicsSystem::clearCell(const std::string& cellId)
{
    for (auto it = mObjects.begin(); it != mObjects.end(); )
    {
        if (it->second.cellId == cellId) {
            if (mDragKey == it->first) mDragKey.clear();
            it = mObjects.erase(it);
        } else ++it;
    }
}

bool AddPhysicsSystem::isRegistered(const MWWorld::Ptr& ptr) const
{
    if (ptr.isEmpty()) return false;
    return mObjects.count(makeKey(ptr.getCellRef().getRefId(),
                                   ptr.getCell()->getCell()->getDescription())) > 0;
}

// ── Drag ─────────────────────────────────────────────────────────────────

void AddPhysicsSystem::startOrContinueDrag(const MWWorld::Ptr& ptr,
                                            const osg::Vec3f& target,
                                            float dt)
{
    if (ptr.isEmpty()) return;

    const std::string refId  = ptr.getCellRef().getRefId();
    const std::string cellId = ptr.getCell()->getCell()->getDescription();
    const std::string key    = makeKey(refId, cellId);

    // Авто-регистрируем если не было
    if (!mObjects.count(key))
        registerObject(ptr, osg::Vec3f(0,0,0));

    mDragKey = key;
    auto& obj = mObjects[key];
    obj.sleeping = false;

    // Пружинное притяжение к target (spring-damper)
    osg::Vec3f delta = target - obj.position;
    float dist = delta.length();
    if (dist < 0.5f) return;

    // k=300 (жёсткость), d=15 (демпфирование)
    osg::Vec3f spring = delta * (300.f / dist) * std::min(dist, 250.f);
    osg::Vec3f damp   = obj.velocity * (-15.f);
    obj.velocity += (spring + damp) * dt;

    // Ограничение скорости перетаскивания
    const float maxSpeed = 800.f;
    float spd = obj.velocity.length();
    if (spd > maxSpeed)
        obj.velocity *= maxSpeed / spd;
}

void AddPhysicsSystem::releaseDrag()
{
    mDragKey.clear();
}

// ── Raycast / Ground ─────────────────────────────────────────────────────

float AddPhysicsSystem::getGroundZ(const osg::Vec3f& pos) const
{
    const MWPhysics::RayCastingInterface* rc =
        MWBase::Environment::get().getWorld()->getRayCasting();
    if (!rc) return FLT_MIN;

    const osg::Vec3f from = pos + osg::Vec3f(0, 0, 4.f);
    const osg::Vec3f to   = pos + osg::Vec3f(0, 0, -2000.f);

    auto result = rc->castRay(from, to, MWWorld::ConstPtr(),
                               std::vector<MWWorld::Ptr>(),
                               MWPhysics::CollisionType_World |
                               MWPhysics::CollisionType_HeightMap);

    if (!result.mHit) return FLT_MIN;
    return result.mHitPos.z();
}

void AddPhysicsSystem::syncPosition(const PhysicsObject& obj) const
{
    MWWorld::Ptr ptr =
        MWBase::Environment::get().getWorld()->searchPtr(obj.refId, false, false);
    if (ptr.isEmpty()) return;

    MWBase::Environment::get().getWorld()->moveObject(
        ptr, obj.position.x(), obj.position.y(), obj.position.z());

    mwmp::ObjectList* objectList =
        mwmp::Main::get().getNetworking()->getObjectList();
    objectList->reset();
    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
    objectList->addObjectPlace(ptr, true);
    objectList->sendObjectPlace();
}

// ── Step ─────────────────────────────────────────────────────────────────

bool AddPhysicsSystem::stepObject(PhysicsObject& obj, float dt)
{
    if (obj.sleeping) return false;

    // Drag — не применяем гравитацию и трение, только spring update выше
    if (!mDragKey.empty() && makeKey(obj.refId, obj.cellId) == mDragKey)
    {
        osg::Vec3f newPos = obj.position + obj.velocity * dt;
        bool moved = (newPos - obj.position).length() > 0.05f;
        obj.position = newPos;
        return moved;
    }

    // Воздушное сопротивление
    obj.velocity *= std::max(0.f, 1.f - obj.linearDamping * dt);

    // Гравитация
    obj.velocity.z() += GRAVITY * dt;

    osg::Vec3f newPos = obj.position + obj.velocity * dt;

    // Коллизия с землёй
    float gz = getGroundZ(newPos);
    bool onGround = false;

    if (gz != FLT_MIN && newPos.z() - obj.radius < gz)
    {
        onGround = true;
        newPos.z() = gz + obj.radius;
        if (obj.velocity.z() < 0.f)
            obj.velocity.z() = -obj.velocity.z() * obj.bounce;
        obj.velocity.x() *= obj.friction;
        obj.velocity.y() *= obj.friction;
    }

    // Засыпание
    if (obj.velocity.length() < obj.sleepThreshold && onGround)
    {
        obj.timeAsleep += dt;
        if (obj.timeAsleep >= obj.sleepDelay)
        {
            obj.velocity   = osg::Vec3f(0, 0, 0);
            obj.sleeping   = true;
            if (gz != FLT_MIN) newPos.z() = gz + obj.radius;
        }
    }
    else obj.timeAsleep = 0.f;

    bool moved = (newPos - obj.position).length() > 0.05f;
    obj.position = newPos;
    return moved;
}

// ── Main update ───────────────────────────────────────────────────────────

void AddPhysicsSystem::update(float dt)
{
    dt = std::min(dt, 0.1f);
    const int substeps = std::min(MAX_SUBSTEPS,
                                   std::max(1, static_cast<int>(dt / 0.016f)));
    const float subDt = dt / static_cast<float>(substeps);

    for (auto& kv : mObjects)
    {
        bool moved = false;
        for (int s = 0; s < substeps; ++s)
            moved |= stepObject(kv.second, subDt);
        if (moved)
            syncPosition(kv.second);
    }
}

} // namespace MWMechanics
