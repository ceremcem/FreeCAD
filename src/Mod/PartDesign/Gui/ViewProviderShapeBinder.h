/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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


#ifndef PARTGUI_ViewProviderShapeBinder_H
#define PARTGUI_ViewProviderShapeBinder_H

#include <boost/signals2.hpp>
#include <Gui/ViewProviderPythonFeature.h>
#include <Mod/Part/Gui/ViewProvider.h>

namespace PartDesignGui {

// TODO may be derive from something else e.g. ViewProviderGeometryObject (2015-09-11, Fat-Zer)
class PartDesignGuiExport ViewProviderShapeBinder : public PartGui::ViewProviderPart
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderShapeBinder);

public:
    /// Constructor
    ViewProviderShapeBinder();
    virtual ~ViewProviderShapeBinder();

    void setupContextMenu(QMenu*, QObject*, const char*) override;
    void highlightReferences(const bool on, bool auxiliary);
    
protected:
    virtual bool setEdit(int ModNum) override;
    virtual void unsetEdit(int ModNum) override;
    
private:
    std::vector<App::Color> originalLineColors;
    std::vector<App::Color> originalFaceColors;

};

class PartDesignGuiExport ViewProviderSubShapeBinder : public PartGui::ViewProviderPart
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderShapeBinder);
    typedef PartGui::ViewProviderPart inherited;

public:
    App::PropertyBool UseBinderStyle;

    /// Constructor
    ViewProviderSubShapeBinder();

    bool canDropObjects() const override {return true;}
    bool canDragAndDropObject(App::DocumentObject*) const override {return false;}
    bool canDropObjectEx(App::DocumentObject *obj, App::DocumentObject *owner, 
            const char *subname, const std::vector<std::string> &elements) const override;
    std::string dropObjectEx(App::DocumentObject*, App::DocumentObject*, const char *, 
            const std::vector<std::string> &) override;
    std::vector<App::DocumentObject*> claimChildren(void) const override;

    virtual bool doubleClicked() override;
    virtual void setupContextMenu(QMenu* menu, QObject* receiver, const char* member) override;
    virtual bool setEdit(int ModNum) override;
    virtual void attach(App::DocumentObject *obj) override;
    virtual void onChanged(const App::Property *prop) override;
    virtual void getExtraIcons(std::vector<std::pair<QByteArray, QPixmap> > &) const override;
    virtual QString getToolTip(const QByteArray &tag) const override;
    virtual bool iconMouseEvent(QMouseEvent *, const QByteArray &tag) override;
    virtual void updateData(const App::Property*) override;
    virtual Gui::ViewProviderDocumentObject *getLinkedViewProvider(
            std::string *subname=0, bool recursive=false) const override;

private:
    void updatePlacement(bool transaction);
    void generateIcons() const;

private:
    struct IconInfo {
        std::vector<App::SubObjectT> objs;
        QPixmap pixmap;
    };
    mutable std::map<QByteArray, IconInfo> iconMap;
    std::vector<boost::signals2::scoped_connection> iconChangeConns;
    int _dropID = 0;
};

typedef Gui::ViewProviderPythonFeatureT<ViewProviderSubShapeBinder> ViewProviderSubShapeBinderPython;

} // namespace PartDesignGui


#endif // PARTGUI_ViewProviderShapeBinder_H
