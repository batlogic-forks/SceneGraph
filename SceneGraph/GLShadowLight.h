#pragma once

#include <pangolin/gl.h>
#include <SceneGraph/SceneGraph.h>
#include <SceneGraph/GLLight.h>

namespace SceneGraph
{

class GLShadowLight : public GLLight
{
public:
    GLShadowLight(double x=0, double y=0, double z=0, int shadowBufferWidth = 1024, int shadowBufferHeight = 1024)
        : GLLight(x,y,z),
          fb_img(shadowBufferWidth,shadowBufferHeight),
          fb_depth(shadowBufferWidth,shadowBufferHeight,GL_DEPTH_COMPONENT),
          framebuffer(fb_img,fb_depth),
          depth_tex(shadowBufferWidth,shadowBufferHeight,GL_DEPTH_COMPONENT,true,1,GL_DEPTH_COMPONENT,GL_UNSIGNED_BYTE)
    {
        // Setup border so we don't cast shadows beyond shadow map.
        depth_tex.Bind();
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        GLfloat color[4] = {1,1,1,1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, color);
        CheckForGLErrors();
    }

    void AddShadowCaster(GLObject* obj)
    {
        shadow_casters.push_back(obj);
    }

    void AddShadowReceiver(GLObject* obj)
    {
        shadow_receivers.push_back(obj);
    }

    void AddShadowCasterAndReceiver(GLObject* obj)
    {
        shadow_casters.push_back(obj);
        shadow_receivers.push_back(obj);
    }


    virtual void PreRender(GLSceneGraph& /*scene*/) {
        SetupLight();
        ComputeShadows();

        ApplyAsGlLight(GL_LIGHT0);
        GLfloat ambientLight [] = {0.2, 0.2, 0.2, 1.0};
        GLfloat diffuseLight [] = {0.4, 0.4, 0.4, 1.0};
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);
    }

    virtual void PostRender(GLSceneGraph& /*scene*/) {
        GLfloat lowAmbient[4] = {0.01f, 0.01f, 0.01f, 1.0f};
        GLfloat lowDiffuse[4] = {0.01f, 0.01f, 0.01f, 1.0f};
        glLightfv(GL_LIGHT0, GL_AMBIENT, lowAmbient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, lowDiffuse);
        DrawShadows();
        glDisable( GL_LIGHT0 );
    }

    void ComputeShadows()
    {
        // Save ModelView and Projection
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glMatrixMode(GL_MODELVIEW);

        // Put depthmap from light into depth_tex
        glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_COLOR_BUFFER_BIT | GL_VIEWPORT_BIT);
        framebuffer.Bind();
        glShadeModel(GL_FLAT);
        glDisable(GL_LIGHTING);
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_NORMALIZE);
        glColorMask(0, 0, 0, 0);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0, 4.0);

        glViewport(0,0,fb_img.width,fb_img.height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        stacks_light.Apply();
        for(unsigned int i=0; i<shadow_casters.size(); ++i ) {
            shadow_casters[i]->DrawObjectAndChildren(eRenderNoPrePostHooks);
        }

        depth_tex.Bind();
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 0, 0, fb_img.width, fb_img.height, 0);

        framebuffer.Unbind();
        glPopAttrib();
        CheckForGLErrors();

        // Restore ModelView and Projection matrices
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    void DrawShadows()
    {
        // TODO: Probably use shader
        // e.g. http://fabiensanglard.net/shadowmapping/index.php

        glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_COLOR_BUFFER_BIT);

        glActiveTextureARB(GL_TEXTURE1_ARB);
        stacks_light.EnableProjectiveTexturing();

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        depth_tex.Bind();

        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_INTENSITY);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE_ARB, GL_COMPARE_R_TO_TEXTURE );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC_ARB, GL_GEQUAL );
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FAIL_VALUE_ARB, 0.5f);

        glAlphaFunc(GL_GREATER, 0.9f);
        glEnable(GL_ALPHA_TEST);

        glEnable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);

        for(unsigned int i=0; i<shadow_receivers.size(); ++i ) {
            shadow_receivers[i]->DrawObjectAndChildren(eRenderNoPrePostHooks);
        }

        glActiveTextureARB(GL_TEXTURE1_ARB);
        glDisable(GL_TEXTURE_2D);
        glActiveTextureARB(GL_TEXTURE0_ARB);

        glPopAttrib();
        CheckForGLErrors();
    }

protected:
    inline AxisAlignedBoundingBox ObjectBounds(std::vector<GLObject*> objs)
    {
        AxisAlignedBoundingBox bbox;
        for(std::vector<GLObject*>::const_iterator i=objs.begin(); i!= objs.end(); ++i) {
            bbox.Insert((*i)->GetPose4x4_po(), (*i)->ObjectAndChildrenBounds() );
        }
        return bbox;
    }

    inline void SetupLight()
    {
        // TODO: Ideally we would work out a tight fit around shadow_casters only
        const AxisAlignedBoundingBox bboxcasters = ObjectBounds(shadow_casters);
        const AxisAlignedBoundingBox bboxreceivers = ObjectBounds(shadow_receivers);
        AxisAlignedBoundingBox bbox_both;
        bbox_both.Insert(bboxcasters);
        bbox_both.Insert(bboxreceivers);

        const Eigen::Vector3d lpos = this->GetPose().head<3>();
        const Eigen::Vector3d center = bboxcasters.Center();
        const double dist = (center - lpos).norm();
        const double bothrad = bbox_both.Size().norm() / 2.0f;

        // TODO: Get rid of the hack factor (8)!? Do this properly.
        const double f = fb_img.width * dist / (8*bboxcasters.Size().norm());

        // Point light at scene
        stacks_light.SetProjectionMatrix(pangolin::ProjectionMatrix(fb_img.width,fb_img.height, f, f, fb_img.width/2.0f,fb_img.height/2.0f, std::max(dist-bothrad,0.1), dist+bothrad));
        stacks_light.SetModelViewMatrix(pangolin::ModelViewLookAt(lpos(0), lpos(1), lpos(2), center(0), center(1), center(2), pangolin::AxisNegZ));
    }

    pangolin::GlTexture fb_img;
    pangolin::GlRenderBuffer fb_depth;
    pangolin::GlFramebuffer framebuffer;
    pangolin::GlTexture depth_tex;
    pangolin::OpenGlRenderState stacks_light;

    std::vector<GLObject*> shadow_casters;
    std::vector<GLObject*> shadow_receivers;
};

}