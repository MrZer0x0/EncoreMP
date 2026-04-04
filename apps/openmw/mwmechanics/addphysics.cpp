/*
    addphysics.cpp
    Author: MrZer0
    EncoreMP — C++ port of OpenMWLuaPhysics (original by MaxYari)
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
#include "../mwphysics/raycasting.hpp"      // getRayCasting() — доступен в MWBase::World
#include "../mwphysics/collisiontype.hpp"

// TES3MP networking
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

    if (mObjects.count(key)) return;

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
    mObjects.erase(makeKey(ptr.getCellRef().getRefId(),
                            ptr.getCell()->getCell()->getDescription()));
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
        if (it->second.cellId == cellId)
            it = mObjects.erase(it);
        else
            ++it;
    }
}

bool AddPhysicsSystem::isRegistered(const MWWorld::Ptr& ptr) const
{
    if (ptr.isEmpty()) return false;
    return mObjects.count(makeKey(ptr.getCellRef().getRefId(),
                                   ptr.getCell()->getCell()->getDescription())) > 0;
}

// Используем getRayCasting() — единственный корректный путь к физике через MWBase::World
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
    // searchPtr(name, activeOnly=false) — есть в MWBase::World
    MWWorld::Ptr ptr =
        MWBase::Environment::get().getWorld()->searchPtr(obj.refId, false, false);
    if (ptr.isEmpty()) return;

    MWBase::Environment::get().getWorld()->moveObject(
        ptr, obj.position.x(), obj.position.y(), obj.position.z());

    /*
        Start of tes3mp addition
        Send ObjectPlace so all clients see the moved object.
        Same pattern as sendObjectSound() in spellcasting.cpp.
    */
    mwmp::ObjectList* objectList =
        mwmp::Main::get().getNetworking()->getObjectList();
    objectList->reset();
    objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
    objectList->addObjectPlace(ptr, true);
    objectList->sendObjectPlace();
    /*
        End of tes3mp addition
    */
}

bool AddPhysicsSystem::stepObject(PhysicsObject& obj, float dt)
{
    if (obj.sleeping) return false;

    // Затухание (сопротивление воздуха)
    obj.velocity *= std::max(0.f, 1.f - obj.linearDamping * dt);

    // Гравитация
    obj.velocity.z() += GRAVITY * dt;

    // Интегрирование позиции
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
            if (gz != FLT_MIN)
                newPos.z() = gz + obj.radius;
        }
    }
    else
    {
        obj.timeAsleep = 0.f;
    }

    bool moved = (newPos - obj.position).length() > 0.05f;
    obj.position = newPos;
    return moved;
}

void AddPhysicsSystem::update(float dt)
{
    dt = std::min(dt, 0.1f);
    const int substeps = std::min(MAX_SUBSTEPS, std::max(1, static_cast<int>(dt / 0.016f)));
    const float subDt  = dt / static_cast<float>(substeps);

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
