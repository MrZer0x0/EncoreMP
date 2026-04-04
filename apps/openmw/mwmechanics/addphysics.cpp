/*
    addphysics.cpp
    Author: MrZer0
    EncoreMP C++ port of OpenMWLuaPhysics (original by MaxYari)
*/
/*
    addphysics.cpp  —  EncoreMP C++ port of OpenMWAddPhysics (by MaxYari)
    Original Lua mod: https://github.com/MaxYari/OpenMWAddPhysics
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
#include "../mwphysics/physicssystem.hpp"
#include "../mwphysics/collisiontype.hpp"

// TES3MP networking — same pattern used in spellcasting.cpp / repair.cpp
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"

namespace MWMechanics
{

// -----------------------------------------------------------------------
// makeKey
// -----------------------------------------------------------------------
std::string AddPhysicsSystem::makeKey(const std::string& refId,
                                       const std::string& cellId)
{
    return refId + "@" + cellId;
}

// -----------------------------------------------------------------------
// registerObject
// Call this when the player drops an item (World::placeObject).
// mirrors: PhysicsEngineLocal.lua  onActive / initialise
// -----------------------------------------------------------------------
void AddPhysicsSystem::registerObject(const MWWorld::Ptr& ptr,
                                       const osg::Vec3f& initialVelocity)
{
    if (ptr.isEmpty()) return;

    const std::string refId  = ptr.getCellRef().getRefId();
    const std::string cellId = ptr.getCell()->getCell()->getDescription();
    const std::string key    = makeKey(refId, cellId);

    if (mObjects.count(key)) return; // already tracked

    PhysicsObject obj;
    obj.refId    = refId;
    obj.cellId   = cellId;
    obj.position = ptr.getRefData().getPosition().asVec3();
    obj.velocity = initialVelocity;

    // auto-detect weight from encumbrance (mirrors Lua weight detection)
    if (ptr.getClass().hasToolTip(ptr))
    {
        float w = ptr.getClass().getWeight(ptr);
        obj.weight = std::max(0.1f, w);
    }

    obj.sleeping = (initialVelocity.length() < obj.sleepThreshold);

    mObjects[key] = obj;
}

// -----------------------------------------------------------------------
// removeObject
// -----------------------------------------------------------------------
void AddPhysicsSystem::removeObject(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty()) return;
    mObjects.erase(makeKey(ptr.getCellRef().getRefId(),
                            ptr.getCell()->getCell()->getDescription()));
}

// -----------------------------------------------------------------------
// applyImpulse
// mirrors: physObject:applyImpulse(impulse)
// -----------------------------------------------------------------------
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

// -----------------------------------------------------------------------
// clearCell  — called on cell unload
// -----------------------------------------------------------------------
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

// -----------------------------------------------------------------------
// isRegistered
// -----------------------------------------------------------------------
bool AddPhysicsSystem::isRegistered(const MWWorld::Ptr& ptr) const
{
    if (ptr.isEmpty()) return false;
    return mObjects.count(makeKey(ptr.getCellRef().getRefId(),
                                   ptr.getCell()->getCell()->getDescription())) > 0;
}

// -----------------------------------------------------------------------
// getGroundZ
// Uses the same PhysicsSystem::castRay available in OpenMW 0.47.
// Returns FLT_MIN if no ground found within 2000 units below.
// -----------------------------------------------------------------------
float AddPhysicsSystem::getGroundZ(const osg::Vec3f& pos) const
{
    MWPhysics::PhysicsSystem* physics =
        MWBase::Environment::get().getWorld()->getPhysicsSystem();
    if (!physics) return FLT_MIN;

    const osg::Vec3f from = pos + osg::Vec3f(0, 0, 4.f);
    const osg::Vec3f to   = pos + osg::Vec3f(0, 0, -2000.f);

    // CollisionType_World | CollisionType_HeightMap — no actors, no doors
    auto result = physics->castRay(from, to, MWWorld::ConstPtr(),
                                   std::vector<MWWorld::Ptr>(),
                                   MWPhysics::CollisionType_World |
                                   MWPhysics::CollisionType_HeightMap);

    if (!result.mHit) return FLT_MIN;
    return result.mHitPos.z();
}

// -----------------------------------------------------------------------
// syncPosition
// Moves the actual game object and sends a TES3MP ObjectPlace packet.
// Pattern taken from EncoreMP spellcasting.cpp / repair.cpp networking.
// -----------------------------------------------------------------------
void AddPhysicsSystem::syncPosition(const PhysicsObject& obj) const
{
    // Look up the live Ptr from refId
    MWWorld::Ptr ptr =
        MWBase::Environment::get().getWorld()->searchPtrViaRefId(obj.refId);
    if (ptr.isEmpty()) return;

    // Move the renderer / physics representation
    MWBase::Environment::get().getWorld()->moveObject(
        ptr, obj.position.x(), obj.position.y(), obj.position.z());

    /*
        Start of tes3mp addition

        Send an ID_OBJECT_PLACE packet so all clients see the moved object.
        Same pattern as objectList->sendObjectSound() in spellcasting.cpp.
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

// -----------------------------------------------------------------------
// stepObject
// Single-frame integration. Returns true if object moved.
// mirrors: PhysicsObject:step() in PhysicsObject.lua
// -----------------------------------------------------------------------
bool AddPhysicsSystem::stepObject(PhysicsObject& obj, float dt)
{
    if (obj.sleeping) return false;

    // --- Air resistance (linear damping) ---
    float damping = std::max(0.f, 1.f - obj.linearDamping * dt);
    obj.velocity *= damping;

    // --- Gravity ---
    obj.velocity.z() += GRAVITY * dt;

    // --- Integrate ---
    osg::Vec3f newPos = obj.position + obj.velocity * dt;

    // --- Ground collision ---
    float gz = getGroundZ(newPos);
    bool onGround = false;

    if (gz != FLT_MIN && newPos.z() - obj.radius < gz)
    {
        onGround = true;
        newPos.z() = gz + obj.radius;

        // Reflect vertical velocity with bounce attenuation
        if (obj.velocity.z() < 0.f)
            obj.velocity.z() = -obj.velocity.z() * obj.bounce;

        // Horizontal friction
        obj.velocity.x() *= obj.friction;
        obj.velocity.y() *= obj.friction;
    }

    // --- Sleep logic (mirrors PhysicsObject.lua sleep detection) ---
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

// -----------------------------------------------------------------------
// update  — called every frame from World::update()
// -----------------------------------------------------------------------
void AddPhysicsSystem::update(float dt)
{
    dt = std::min(dt, 0.1f); // guard against lag spikes

    const int substeps = std::min(MAX_SUBSTEPS,
                                   std::max(1, static_cast<int>(dt / 0.016f)));
    const float subDt = dt / static_cast<float>(substeps);

    for (auto& kv : mObjects)
    {
        PhysicsObject& obj = kv.second;
        bool moved = false;

        for (int s = 0; s < substeps; ++s)
            moved |= stepObject(obj, subDt);

        if (moved)
            syncPosition(obj);
    }
}

} // namespace MWMechanics
