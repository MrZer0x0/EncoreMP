/*
    addphysics.hpp / addphysics.cpp
    Author: MrZer0
    EncoreMP C++ port of OpenMWLuaPhysics (original by MaxYari)
*/
#ifndef GAME_MWMECHANICS_ADDPHYSICS_H
#define GAME_MWMECHANICS_ADDPHYSICS_H

/*
    addphysics.hpp  —  EncoreMP C++ port of OpenMWAddPhysics (by MaxYari)
    Original Lua mod: https://github.com/MaxYari/OpenMWAddPhysics

    Drop items are now simulated with gravity, bounce, friction, and sleep.
    Hitting a non-actor object sends it flying.
    All positions are synced to TES3MP clients via ObjectList packets.

    OpenMWAddPhysics originally required OpenMW 0.48+ Lua API.
    EncoreMP is OpenMW 0.47, so the physics is re-implemented here in C++
    using the PhysicsSystem::castRay that already exists in 0.47.
*/

#include <string>
#include <unordered_map>
#include <osg/Vec3f>

namespace MWWorld { class Ptr; }

namespace MWMechanics
{

// -----------------------------------------------------------------------
// PhysicsObject  —  mirrors PhysicsObject.lua from the original mod
// -----------------------------------------------------------------------
struct PhysicsObject
{
    osg::Vec3f position;
    osg::Vec3f velocity;

    // Properties (same names as the Lua original)
    float weight          = 1.f;
    float bounce          = 0.30f;   // 0 = no bounce, 1 = perfect
    float friction        = 0.75f;   // horizontal speed multiplier on ground contact
    float linearDamping   = 0.015f;  // air resistance (fraction per second)
    float sleepThreshold  = 12.f;    // speed (units/s) below which object tries to sleep
    float sleepDelay      = 0.35f;   // seconds of slow speed before object sleeps
    float radius          = 14.f;    // collision sphere radius (units)

    bool  sleeping        = true;
    float timeAsleep      = 0.f;

    // Internal bookkeeping
    std::string refId;
    std::string cellId;
};

// -----------------------------------------------------------------------
// AddPhysicsSystem
// -----------------------------------------------------------------------
class AddPhysicsSystem
{
public:
    AddPhysicsSystem()  = default;
    ~AddPhysicsSystem() = default;

    // Called every frame from World::update()
    void update(float dt);

    // Register a world object for simulation (call when item is dropped)
    void registerObject(const MWWorld::Ptr& ptr,
                        const osg::Vec3f&  initialVelocity = osg::Vec3f(0,0,0));

    // Remove object (call before ptr is deleted or picked up)
    void removeObject(const MWWorld::Ptr& ptr);

    // Instant velocity change — mirrors physObject:applyImpulse()
    void applyImpulse(const MWWorld::Ptr& ptr, const osg::Vec3f& impulse);

    // Remove all objects belonging to a cell (call on cell unload)
    void clearCell(const std::string& cellId);

    bool isRegistered(const MWWorld::Ptr& ptr) const;

private:
    std::unordered_map<std::string, PhysicsObject> mObjects;

    bool  stepObject(PhysicsObject& obj, float dt);
    float getGroundZ(const osg::Vec3f& pos) const;
    void  syncPosition(const PhysicsObject& obj) const;

    static std::string makeKey(const std::string& refId, const std::string& cellId);

    static constexpr float GRAVITY     = -860.f; // units / s²
    static constexpr int   MAX_SUBSTEPS = 4;
};

} // namespace MWMechanics

#endif
