/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef PART_FEATURE_H
#define PART_FEATURE_H

#include <boost/container/map.hpp>

#include "TopoShape.h"
#include "PropertyTopoShape.h"
#include <App/GeoFeature.h>
#include <App/FeaturePython.h>
#include <App/PropertyGeo.h>
// includes for findAllFacesCutBy()
#include <TopoDS_Face.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
class gp_Dir;

class BRepBuilderAPI_MakeShape;

namespace Part
{

class PartFeaturePy;

/** Base class of all shape feature classes in FreeCAD
 */
class PartExport Feature : public App::GeoFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Feature);

public:
    /// Constructor
    Feature(void);
    virtual ~Feature();

    PropertyPartShape Shape;
    App::PropertyLinkSubHidden ColoredElements;

    /** @name methods override feature */
    //@{
    virtual short mustExecute() const override;
    //@}

    /// returns the type name of the ViewProvider
    virtual const char* getViewProviderName() const override;
    virtual const App::PropertyComplexGeoData* getPropertyOfGeometry() const override;

    virtual PyObject* getPyObject(void) override;

    struct HistoryItem {
        App::DocumentObject *obj;
        long tag;
        std::string element;
        std::vector<std::string> intermediates;
        HistoryItem(App::DocumentObject *obj, const char *name)
            :obj(obj),tag(0),element(name)
        {
            if(obj)
                tag = obj->getID();
        }
    };
    static std::list<HistoryItem> getElementHistory(App::DocumentObject *obj,
            const char *name, bool recursive=true, bool sameType=false);

    static std::vector<std::pair<std::string,std::string> > 
    getRelatedElements(App::DocumentObject *obj, const char *name, bool sameType=true, bool withCache=true);

    TopLoc_Location getLocation() const;

    virtual DocumentObject *getSubObject(const char *subname, PyObject **pyObj, 
            Base::Matrix4D *mat, bool transform, int depth) const override;

    /** Convenience function to extract shape from fully qualified subname 
     *
     * @param obj: the parent object
     *
     * @param subname: dot separated full qualified subname
     *
     * @param needSubElement: whether to ignore the non-object subelement
     * reference inside \c subname
     *
     * @param pmat: used as current transformation on input, and return the
     * accumulated transformation on output
     *
     * @param owner: return the owner of the shape returned
     *
     * @param resolveLink: if true, resolve link(s) of the returned 'owner'
     * by calling its getLinkedObject(true) function
     *
     * @param transform: if true, apply obj's transformation. Set to false
     * if pmat already include obj's transformation matrix.
     */
    static TopoDS_Shape getShape(const App::DocumentObject *obj,
            const char *subname=0, bool needSubElement=false, Base::Matrix4D *pmat=0, 
            App::DocumentObject **owner=0, bool resolveLink=true, bool transform=true);

    static TopoShape getTopoShape(const App::DocumentObject *obj,
            const char *subname=0, bool needSubElement=false, Base::Matrix4D *pmat=0, 
            App::DocumentObject **owner=0, bool resolveLink=true, bool transform=true, 
            bool noElementMap=false);

    static App::DocumentObject *getShapeOwner(const App::DocumentObject *obj, const char *subname=0);

    static bool hasShapeOwner(const App::DocumentObject *obj, const char *subname=0) {
        auto owner = getShapeOwner(obj,subname);
        return owner && owner->isDerivedFrom(getClassTypeId());
    }

    virtual const std::vector<std::string>& searchElementCache(const std::string &element,
                                                               bool checkGeometry = true,
                                                               double tol = 1e-7,
                                                               double atol = 1e-10) const override;

    virtual const std::vector<const char*>& getElementTypes(bool all=false) const override;

protected:
    /// recompute only this object
    virtual App::DocumentObjectExecReturn *recompute() override;
    /// recalculate the feature
    virtual App::DocumentObjectExecReturn *execute() override;
    virtual void onBeforeChange(const App::Property* prop) override;
    virtual void onChanged(const App::Property* prop) override;

private:
    struct ElementCache;
    boost::container::map<std::string, ElementCache> _elementCache;
};

class FilletBase : public Part::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::FilletBase);

public:
    FilletBase();

    App::PropertyLink   Base;
    PropertyFilletEdges Edges;
    App::PropertyLinkSub   EdgeLinks;

    virtual short mustExecute() const override;
    virtual void onUpdateElementReference(const App::Property *prop) override;

protected:
    virtual void onDocumentRestored() override;
    virtual void onChanged(const App::Property *) override;
    void syncEdgeLink();
};

typedef App::FeaturePythonT<Feature> FeaturePython;


/** Base class of all shape feature classes in FreeCAD
 */
class PartExport FeatureExt : public Feature
{
    PROPERTY_HEADER(Part::FeatureExt);

public:
    const char* getViewProviderName(void) const {
        return "PartGui::ViewProviderPartExt";
    }
};

// Utility methods
/**
 * Find all faces cut by a line through the centre of gravity of a given face
 * Useful for the "up to face" options to pocket or pad
 */
struct cutFaces {
    TopoShape face;
    double distsq;
};

PartExport
std::vector<cutFaces> findAllFacesCutBy(const TopoShape& shape,
                                        const TopoShape& face, const gp_Dir& dir);

/**
  * Check for intersection between the two shapes. Only solids are guaranteed to work properly
  * There are two modes:
  * 1. Bounding box check only - quick but inaccurate
  * 2. Bounding box check plus (if necessary) boolean operation - costly but accurate
  * Return true if the shapes intersect, false if they don't
  * The flag touch_is_intersection decides whether shapes touching at distance zero are regarded
  * as intersecting or not
  * 1. If set to true, a true check result means that a boolean fuse operation between the two shapes
  *    will return a single solid
  * 2. If set to false, a true check result means that a boolean common operation will return a
  *    valid solid
  * If there is any error in the boolean operations, the check always returns false
  */
PartExport
bool checkIntersection(const TopoDS_Shape& first, const TopoDS_Shape& second,
                       const bool quick, const bool touch_is_intersection);

} //namespace Part


#endif // PART_FEATURE_H

