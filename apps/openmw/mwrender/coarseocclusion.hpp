#ifndef OPENMW_MWRENDER_COARSEOCCLUSION_H
#define OPENMW_MWRENDER_COARSEOCCLUSION_H

#include <vector>

#include <osg/BoundingBox>
#include <osg/BoundingSphere>
#include <osg/Matrix>
#include <osg/observer_ptr>
#include <osg/ref_ptr>
#include <osg/Vec3f>

namespace osg
{
    class Camera;
    class Node;
}

namespace MWWorld
{
    class ConstPtr;
}

namespace MWRender
{
    class CoarseOcclusionCuller
    {
    public:
        CoarseOcclusionCuller();

        void configureFromSettings();
        void rebuild(const osg::Camera* camera, osg::Node* sceneRoot, const osg::Vec3f& eyePoint);
        bool isVisible(const MWWorld::ConstPtr& ptr) const;

    private:
        struct Rect
        {
            int mMinX;
            int mMinY;
            int mMaxX;
            int mMaxY;
            float mNearestDepth;
            float mFarthestDepth;
            bool mValid;

            Rect()
                : mMinX(0)
                , mMinY(0)
                , mMaxX(0)
                , mMaxY(0)
                , mNearestDepth(0.f)
                , mFarthestDepth(0.f)
                , mValid(false)
            {
            }
        };

        class Collector;

        Rect projectSphere(const osg::BoundingSphere& bound) const;
        void rasterizeRect(const Rect& rect);
        bool rectIsVisible(const Rect& rect) const;
        bool sphereContainsEye(const osg::BoundingSphere& bound) const;

    private:
        bool mEnabled;
        bool mUseTerrain;
        bool mUseStatics;
        bool mDebugOverlay;

        int mWidth;
        int mHeight;

        float mOccluderMinRadius;
        float mOccluderMaxRadius;
        float mOccluderMaxDistance;
        float mShrinkFactor;

        osg::observer_ptr<const osg::Camera> mCamera;
        osg::Vec3f mEyePoint;
        std::vector<float> mDepth;
    };
}

#endif
