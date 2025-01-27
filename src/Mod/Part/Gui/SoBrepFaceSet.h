/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef PARTGUI_SOBREPFACESET_H
#define PARTGUI_SOBREPFACESET_H

#include <Inventor/fields/SoSFInt32.h>
#include <Inventor/fields/SoMFInt32.h>
#include <Inventor/fields/SoSFNode.h>
#include <Inventor/fields/SoSubField.h>
#include <Inventor/fields/SoSFColor.h>
#include <Inventor/nodes/SoSubNode.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoReplacedElement.h>
#include <Inventor/sensors/SoFieldSensor.h>
#include <vector>
#include <memory>
#include <Gui/SoFCSelectionContext.h>

class SoGLCoordinateElement;
class SoTextureCoordinateBundle;

namespace PartGui {

/**
 * First some words to the history and the reason why we have this class:
 * In older FreeCAD versions we had an own Inventor node for each sub-element of a shape with its own highlight node.
 * For more complex objects the number of nodes increased dramatically with the result that interactions with such
 * objects was almost impossible because every little rotation or hovering caused a very time consuming redraw. The
 * most time consuming part was not the OpenGL calls to render the elements but the traversal of these many nodes.
 *
 * So, the idea was to have one node that handles all faces of a shape, one node that handles all edges and another node
 * that handles the vertexes. And each of these nodes manages the highlighting and selection itself.
 *
 * Now to SoBrepFaceSet in detail:
 * The most complex nodes of them is SoBrepFaceSet because it adds some extra logic for the handling of faces. As you can
 * see this class also has the attribute partIndex which is an array of integers. This basically expresses the logical
 * grouping of the faces in the coordIndex field -- the parts. This means that a part in SoBrepFaceSet corresponds to a face
 * in a shape object. Each part value gives you the number of triangles a certain face consists of. That's also the reason why
 * SoBrepFaceSet only renders triangles. If you want a quad to be rendered than create two triangles and set the corresponding
 * part number to 2.
 *
 * Example:
 * Let's say you have a shape with two faces. When meshing face 1 it creates 10 triangles and face 2 creates 5 triangles. Then
 * the partIndex attribute would be the array [10,5].
 * 
 * Highlighting/selection:
 * The highlightIndex now defines which part of the shape must be highlighted. So, in the above example it can have the values
 * 0, 1, or -1 (i.e. no highlighting). The highlightIndex is a SoSFInt32 field because only one part can be highlighted at a
 * moment while selectionIndex is a SoMFInt32 field because several parts can be selected at a time. All the logic how to do the
 * rendering is done inside renderHighlight()/renderSelection().
 *
 * Actually you can access the highlightIndex directly or you can apply a SoHighlightElementAction on it. And don't forget: if you
 * do some mouse picking and you got a SoFaceDetail then use getPartIndex() to get the correct part.
 *
 * As an example how to use the class correctly see ViewProviderPartExt::updateVisual().
 */
class PartGuiExport SoBrepFaceSet : public SoIndexedFaceSet {
    typedef SoIndexedFaceSet inherited;

    SO_NODE_HEADER(SoBrepFaceSet);

public:
    static void initClass();
    SoBrepFaceSet();

    void setSiblings(std::vector<SoNode*> &&);

    SoMFInt32 partIndex;
    SoMFInt32 highlightIndices;
    SoSFColor highlightColor;

    static bool makeDistinctColor(SbColor &res, const SbColor &color, const SbColor &other);
    static bool makeDistinctColor(uint32_t &res, uint32_t color, uint32_t other);

protected:
    virtual ~SoBrepFaceSet();
    virtual void GLRender(SoGLRenderAction *action);
    virtual void GLRenderInPath(SoGLRenderAction *action);
    virtual void GLRenderBelowPath(SoGLRenderAction * action);
    virtual void doAction(SoAction* action); 
    virtual SoDetail * createTriangleDetail(
        SoRayPickAction * action,
        const SoPrimitiveVertex * v1,
        const SoPrimitiveVertex * v2,
        const SoPrimitiveVertex * v3,
        SoPickedPoint * pp);
    virtual void generatePrimitives(SoAction * action);
    virtual void getBoundingBox(SoGetBoundingBoxAction * action);
    virtual void rayPick(SoRayPickAction *action);

private:
    enum Binding {
        OVERALL = 0,
        PER_PART,
        PER_PART_INDEXED,
        PER_FACE,
        PER_FACE_INDEXED,
        PER_VERTEX,
        PER_VERTEX_INDEXED,
        NONE = OVERALL
    };
    Binding findMaterialBinding(SoState * const state) const;
    Binding findNormalBinding(SoState * const state) const;

    void renderFaces(const SoCoordinateElement *coords,
                     const int32_t *vertexindices,
                     int num_vertexindices,
                     const int32_t *partindices,
                     int start_partindex,
                     int num_partindices,
                     const SbVec3f *normals,
                     const int32_t *normindices,
                     SoMaterialBundle *const materials,
                     const int32_t *matindices,
                     SoTextureCoordinateBundle & tb,
                     const int32_t *texindices,
                     int nbind,
                     int mbind,
                     int texture);

    typedef Gui::SoFCSelectionContextEx SelContext;
    typedef Gui::SoFCSelectionContextExPtr SelContextPtr;

    void glRender(SoGLRenderAction *action, bool inpath);

    void renderShape(SoGLRenderAction *action, SelContextPtr ctx, SelContextPtr ctx2,
                     bool checkTransp, bool shadowRendering=false);
    void renderShape(SoGLRenderAction *action);

    void renderHighlight(SoGLRenderAction *action, SelContextPtr);
    void renderSelection(SoGLRenderAction *action, SelContextPtr, bool push=true);
    void _renderSelection(SoGLRenderAction *action, SbColor color, bool push);

    int overrideMaterialBinding(SoGLRenderAction *action,
            int selected, SelContextPtr ctx, SelContextPtr ctx2);

    void onPartIndexChange();
    void sortParts(SoState *state, SelContextPtr ctx, SelContextPtr ctx2,
                   const float *trans, int numtrans, bool shadow);
    void buildPartBBoxes(SoState *state);
    void buildPartIndexCache();
    int getPartFromFace(int index);
    bool isHighlightAll(const SelContextPtr &ctx);
    bool isSelectAll(const SelContextPtr &ctx);

    void generatePrimitivesRange(SoAction * action, int pstart, int fstart, int vstart, int vend);

private:
    SelContextPtr selContext;
    SelContextPtr selContext2;
    std::map<int32_t, int32_t> partIndexMap;
    std::vector<int32_t> indexOffset;
    std::vector<int32_t> matIndex;
    std::vector<uint32_t> packedColors;
    uint32_t packedColor;

    float overrideTransparency;
    SbColor hiddenLineColor;
    SoColorPacker packer;

    Gui::SoFCSelectionCounter selCounter;
    std::vector<SoNode*> siblings;

    SoFieldSensor partIndexSensor;
    std::vector<SbBox3f> partBBoxes;

    // Define some VBO pointer for the current mesh
    class VBO;
    std::unique_ptr<VBO> pimpl;
};

} // namespace PartGui

#endif // PARTGUI_SOBREPFACESET_H

