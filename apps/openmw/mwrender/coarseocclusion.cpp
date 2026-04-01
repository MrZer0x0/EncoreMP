#include "coarseocclusion.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <osg/Camera>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osg/Vec4f>

#include <components/settings/settings.hpp>

#include "../mwworld/ptr.hpp"

#include "vismask.hpp"

namespace
{
    static float clamp01(float v)
    {
        return std::max(0.f, std::min(1.f, v));
    }
}

namespace MWRender
{
    class CoarseOcclusionCuller::Collector : public osg::NodeVisitor
    {
    public:
        Collector(CoarseOcclusionCuller* culler, unsigned int mask, bool respectRadius)
            : osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN)
            , mCuller(culler)
            , mMask(mask)
            , mRespectRadius(respectRadius)
        {
        }

        void apply(osg::Node& node) override
        {
            if ((node.getNodeMask() & mMask) == 0)
            {
                traverse(node);
                return;
            }

            const osg::BoundingSphere& bound = node.getBound();
            if (!bound.valid())
            {
                traverse(node);
                return;
            }

            const float radius = bound.radius();
            const float distance = (bound.center() - mCuller->mEyePoint).length();

            if (mRespectRadius)
            {
                if (radius < mCuller->mOccluderMinRadius || radius > mCuller->mOccluderMaxRadius)
                {
                    traverse(node);
                    return;
                }
            }

            if (distance - radius > mCuller->mOccluderMaxDistance)
            {
                traverse(node);
                return;
            }

            osg::BoundingSphere shrunk = bound;
            shrunk._radius *= mCuller->mShrinkFactor;
            mCuller->rasterizeRect(mCuller->projectSphere(shrunk));
            traverse(node);
        }

    private:
        CoarseOcclusionCuller* mCuller;
        unsigned int mMask;
        bool mRespectRadius;
    };

    CoarseOcclusionCuller::CoarseOcclusionCuller()
        : mEnabled(false)
        , mUseTerrain(true)
        , mUseStatics(true)
        , mDebugOverlay(false)
        , mWidth(512)
        , mHeight(256)
        , mOccluderMinRadius(300.f)
        , mOccluderMaxRadius(5000.f)
        , mOccluderMaxDistance(6144.f)
        , mShrinkFactor(1.f)
    {
    }

    void CoarseOcclusionCuller::configureFromSettings()
    {
        mEnabled = Settings::Manager::getBool("occlusion culling", "Camera");
        mUseTerrain = Settings::Manager::getBool("occlusion culling terrain", "Camera");
        mUseStatics = Settings::Manager::getBool("occlusion culling statics", "Camera");
        mDebugOverlay = Settings::Manager::getBool("occlusion debug overlay", "Camera");

        mWidth = std::max(32, Settings::Manager::getInt("occlusion buffer width", "Camera"));
        mHeight = std::max(16, Settings::Manager::getInt("occlusion buffer height", "Camera"));
        mOccluderMinRadius = std::max(0.f, Settings::Manager::getFloat("occlusion occluder min radius", "Camera"));
        mOccluderMaxRadius = std::max(mOccluderMinRadius, Settings::Manager::getFloat("occlusion occluder max radius", "Camera"));
        mOccluderMaxDistance = std::max(128.f, Settings::Manager::getFloat("occlusion occluder max distance", "Camera"));
        mShrinkFactor = std::max(0.1f, std::min(1.f, Settings::Manager::getFloat("occlusion occluder shrink factor", "Camera")));

        mDepth.assign(static_cast<std::size_t>(mWidth * mHeight), std::numeric_limits<float>::infinity());
    }

    void CoarseOcclusionCuller::rebuild(const osg::Camera* camera, osg::Node* sceneRoot, const osg::Vec3f& eyePoint)
    {
        mCamera = camera;
        mEyePoint = eyePoint;

        if (!mEnabled || !mCamera)
            return;

        std::fill(mDepth.begin(), mDepth.end(), std::numeric_limits<float>::infinity());

        if (sceneRoot)
        {
            if (mUseTerrain)
            {
                Collector terrainCollector(this, Mask_Terrain, false);
                sceneRoot->accept(terrainCollector);
            }

            if (mUseStatics)
            {
                Collector staticCollector(this, Mask_Static | Mask_Object, true);
                sceneRoot->accept(staticCollector);
            }
        }

        (void)mDebugOverlay;
    }

    CoarseOcclusionCuller::Rect CoarseOcclusionCuller::projectSphere(const osg::BoundingSphere& bound) const
    {
        Rect rect;
        if (!mCamera || !bound.valid())
            return rect;

        osg::Matrix viewProj = mCamera->getViewMatrix() * mCamera->getProjectionMatrix();

        osg::Vec3f c = bound.center();
        const float r = bound.radius();

        osg::Vec3f corners[8] = {
            osg::Vec3f(c.x()-r, c.y()-r, c.z()-r),
            osg::Vec3f(c.x()-r, c.y()-r, c.z()+r),
            osg::Vec3f(c.x()-r, c.y()+r, c.z()-r),
            osg::Vec3f(c.x()-r, c.y()+r, c.z()+r),
            osg::Vec3f(c.x()+r, c.y()-r, c.z()-r),
            osg::Vec3f(c.x()+r, c.y()-r, c.z()+r),
            osg::Vec3f(c.x()+r, c.y()+r, c.z()-r),
            osg::Vec3f(c.x()+r, c.y()+r, c.z()+r)
        };

        float minX = 1.f;
        float minY = 1.f;
        float maxX = 0.f;
        float maxY = 0.f;
        float nearestDepth = 1.f;
        float farthestDepth = 0.f;
        bool any = false;

        for (int i = 0; i < 8; ++i)
        {
            osg::Vec4f clip = osg::Vec4f(corners[i], 1.f) * viewProj;
            if (std::abs(clip.w()) < 1e-6f)
                continue;

            osg::Vec3f ndc(clip.x()/clip.w(), clip.y()/clip.w(), clip.z()/clip.w());
            if (ndc.z() < -1.2f || ndc.z() > 1.2f)
                continue;

            any = true;
            minX = std::min(minX, clamp01(ndc.x() * 0.5f + 0.5f));
            maxX = std::max(maxX, clamp01(ndc.x() * 0.5f + 0.5f));
            minY = std::min(minY, clamp01(0.5f - ndc.y() * 0.5f));
            maxY = std::max(maxY, clamp01(0.5f - ndc.y() * 0.5f));

            const float depth01 = clamp01(ndc.z() * 0.5f + 0.5f);
            nearestDepth = std::min(nearestDepth, depth01);
            farthestDepth = std::max(farthestDepth, depth01);
        }

        if (!any || maxX <= minX || maxY <= minY)
            return rect;

        rect.mValid = true;
        rect.mMinX = std::max(0, static_cast<int>(std::floor(minX * mWidth)));
        rect.mMaxX = std::min(mWidth - 1, static_cast<int>(std::ceil(maxX * mWidth)));
        rect.mMinY = std::max(0, static_cast<int>(std::floor(minY * mHeight)));
        rect.mMaxY = std::min(mHeight - 1, static_cast<int>(std::ceil(maxY * mHeight)));
        rect.mNearestDepth = nearestDepth;
        rect.mFarthestDepth = farthestDepth;
        return rect;
    }

    void CoarseOcclusionCuller::rasterizeRect(const Rect& rect)
    {
        if (!rect.mValid)
            return;

        for (int y = rect.mMinY; y <= rect.mMaxY; ++y)
        {
            for (int x = rect.mMinX; x <= rect.mMaxX; ++x)
            {
                float& cell = mDepth[static_cast<std::size_t>(y * mWidth + x)];
                cell = std::min(cell, rect.mNearestDepth);
            }
        }
    }

    bool CoarseOcclusionCuller::rectIsVisible(const Rect& rect) const
    {
        if (!rect.mValid)
            return true;

        bool sawOccluder = false;
        for (int y = rect.mMinY; y <= rect.mMaxY; ++y)
        {
            for (int x = rect.mMinX; x <= rect.mMaxX; ++x)
            {
                const float cell = mDepth[static_cast<std::size_t>(y * mWidth + x)];
                if (std::isfinite(cell))
                {
                    sawOccluder = true;
                    if (rect.mNearestDepth <= cell + 0.01f)
                        return true;
                }
                else
                    return true;
            }
        }

        return !sawOccluder ? true : false;
    }

    bool CoarseOcclusionCuller::sphereContainsEye(const osg::BoundingSphere& bound) const
    {
        return (bound.center() - mEyePoint).length2() <= (bound.radius() * bound.radius());
    }

    bool CoarseOcclusionCuller::isVisible(const MWWorld::ConstPtr& ptr) const
    {
        if (!mEnabled || !mCamera)
            return true;

        const osg::Node* node = ptr.getRefData().getBaseNode();
        if (!node)
            return true;

        const osg::BoundingSphere& bound = node->getBound();
        if (!bound.valid())
            return true;

        if (sphereContainsEye(bound))
            return true;

        const Rect rect = projectSphere(bound);
        return rectIsVisible(rect);
    }
}
