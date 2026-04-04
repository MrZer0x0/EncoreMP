#ifndef GAME_MWMECHANICS_ADDPHYSICS_H
#define GAME_MWMECHANICS_ADDPHYSICS_H

/*
    addphysics.hpp
    Author: MrZer0
    EncoreMP — C++ port of OpenMWLuaPhysics (original by MaxYari)

    Физика брошенных/упавших предметов + перетаскивание (E зажать).
*/

#include <string>
#include <unordered_map>
#include <osg/Vec3f>

namespace MWWorld { class Ptr; }

namespace MWMechanics
{

struct PhysicsObject
{
    osg::Vec3f position;
    osg::Vec3f velocity;

    float weight         = 1.f;
    float bounce         = 0.30f;
    float friction       = 0.75f;
    float linearDamping  = 0.015f;
    float sleepThreshold = 12.f;
    float sleepDelay     = 0.35f;
    float radius         = 14.f;

    bool  sleeping   = true;
    float timeAsleep = 0.f;

    std::string refId;
    std::string cellId;
};

class AddPhysicsSystem
{
public:
    AddPhysicsSystem()  = default;
    ~AddPhysicsSystem() = default;

    // Основное обновление — вызывать каждый кадр из World::update()
    void update(float dt);

    // Регистрация предмета (при дропе или первом ударе)
    void registerObject(const MWWorld::Ptr& ptr,
                        const osg::Vec3f&  initialVelocity = osg::Vec3f(0,0,0));

    void removeObject(const MWWorld::Ptr& ptr);

    // Мгновенный импульс — mirrors physObject:applyImpulse()
    void applyImpulse(const MWWorld::Ptr& ptr, const osg::Vec3f& impulse);

    void clearCell(const std::string& cellId);

    bool isRegistered(const MWWorld::Ptr& ptr) const;

    // ── Drag API ─────────────────────────────────────────────────────────
    // Начать/продолжить перетаскивание предмета к точке target.
    // Вызывать каждый кадр пока E зажата.
    void startOrContinueDrag(const MWWorld::Ptr& ptr, const osg::Vec3f& target, float dt);

    // Бросить/отпустить предмет (E отпущена).
    void releaseDrag();

    // Есть ли сейчас активный dragged предмет
    bool isDragging() const { return !mDragKey.empty(); }

    // Ключ перетаскиваемого объекта (для сравнения с getFacedObject)
    const std::string& dragKey() const { return mDragKey; }

private:
    std::unordered_map<std::string, PhysicsObject> mObjects;

    // Drag state
    std::string mDragKey;       // ключ (refId@cellId) перетаскиваемого объекта

    bool  stepObject(PhysicsObject& obj, float dt);
    float getGroundZ(const osg::Vec3f& pos) const;
    void  syncPosition(const PhysicsObject& obj) const;

    static std::string makeKey(const std::string& refId, const std::string& cellId);

    static constexpr float GRAVITY      = -860.f;
    static constexpr int   MAX_SUBSTEPS = 4;
};

} // namespace MWMechanics

#endif
