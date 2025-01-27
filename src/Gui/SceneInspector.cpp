/***************************************************************************
 *   Copyright (c) 2008 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#include "PreCompiled.h"
#ifndef _PreComp_
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoIndexedShape.h>
# include <QHeaderView>
# include <QTextStream>
#endif

#include <Base/Tools.h>
#include <App/Document.h>
#include "SceneInspector.h"
#include "ui_SceneInspector.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "SoFCUnifiedSelection.h"
#include "Document.h"
#include "Application.h"
#include "ViewProviderLink.h"

using namespace Gui::Dialog;

/* TRANSLATOR Gui::Dialog::SceneModel */

SceneModel::SceneModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

SceneModel::~SceneModel()
{
}

void SceneModel::setNodeNames(const QHash<SoNode*, QString>& names)
{
    nodeNames = names;
}

QVariant SceneModel::headerData (int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (role != Qt::DisplayRole)
            return QVariant();
        if (section == 0)
            return tr("Inventor Tree");
        else if (section == 1)
            return tr("Name");
    }

    return QVariant();
}

bool SceneModel::setHeaderData (int, Qt::Orientation, const QVariant &, int)
{
    return false;
}

void SceneModel::setNode(SoNode* node)
{
    this->beginResetModel();
    items.clear();
    rootItem.node = node;
    this->endResetModel();
}

QVariant SceneModel::data(const QModelIndex & index, int role) const
{
    if (role != Qt::DisplayRole || index.column() > 1)
        return QVariant();

    auto it = items.find(index);
    if (it == items.end())
        return QVariant();

    auto &item = it.value();

    if (index.column() == 0)
        return QString::fromLatin1(item.node->getTypeId().getName());

    SoNode *node = item.node.get();
    auto itName = nodeNames.find(node);
    QString name;
    QTextStream stream(&name);
    stream << item.node.get() << ", ";
    if(node->isOfType(SoSwitch::getClassTypeId())) {
        auto pcSwitch = static_cast<SoSwitch*>(node);
        stream << pcSwitch->whichChild.getValue() << ", ";
        if(node->isOfType(SoFCSwitch::getClassTypeId())) {
            auto pathNode = static_cast<SoFCSwitch*>(node);
            stream << pathNode->defaultChild.getValue() << ", "
                << pathNode->overrideSwitch.getValue() << ", ";
        }
    } else if (node->isOfType(SoSeparator::getClassTypeId())) {
        auto pcSeparator = static_cast<SoSeparator*>(node);
        stream << pcSeparator->renderCaching.getValue() << ", "
            << pcSeparator->boundingBoxCaching.getValue() << ", ";
    } else if (node->isOfType(SoIndexedShape::getClassTypeId())) {
        auto shape = static_cast<SoIndexedShape*>(node);
        stream << shape->coordIndex.getNum() << ", ";
    }

    auto obj = ViewProviderLink::linkedObjectByNode(node);
    if (obj) {
        stream << QLatin1String(" -> ");
        if (obj->getDocument() != App::GetApplication().getActiveDocument())
            stream << QString::fromLatin1(obj->getFullName().c_str());
        else
            stream << QString::fromLatin1(obj->getNameInDocument());
        if (obj->Label.getStrValue() != obj->getNameInDocument())
            stream << QString::fromLatin1(" (%1)").arg(QString::fromUtf8(obj->Label.getValue()));
        stream << QLatin1String(", ");
    }

    if (itName != nodeNames.end())
        stream << itName.value();
    else
        stream << node->getName();
    return name;
}

QModelIndex SceneModel::parent(const QModelIndex & index) const
{
    auto it = items.find(index);
    if (it == items.end())
        return QModelIndex();
    return it.value().parent;
}

QModelIndex SceneModel::index(int row, int column, const QModelIndex &parent) const
{
    const Item *item = nullptr;
    if (parent.isValid()) {
        auto it = items.find(parent);
        if (it == items.end())
            return QModelIndex();
        item = &it.value();
    } else
        item = &rootItem;

    SoNode *node = item->node;
    if (autoExpanding && !item->expand)
        return QModelIndex();

    QModelIndex index = createIndex(row, column, (void*)item);

    if (node->isOfType(SoFCPathAnnotation::getClassTypeId())) {
        auto path = static_cast<SoFCPathAnnotation*>(node)->getPath();
        if (!path || row < 0 || row >= path->getLength())
            return QModelIndex();

        auto &child = items[index];
        if (!child.node) {
            child.node = path->getNode(row);
            child.expand = false;
            child.parent = parent;
        }
        return index;
    } else if (node->isOfType(SoGroup::getClassTypeId())) {
        auto group = static_cast<SoGroup*>(node);
        if (row < 0 || row >= group->getNumChildren())
            return QModelIndex();

        auto &child = items[index];
        if (!child.node) {
            child.node = group->getChild(row);
            child.parent = parent;
        }
        return index;
    }
    return QModelIndex();
}

int SceneModel::rowCount(const QModelIndex & parent) const
{
    const Item *item = nullptr;
    if (parent.isValid()) {
        auto it = items.find(parent);
        if (it == items.end())
            return 0;
        item = &it.value();
    } else
        item = &rootItem;
    SoNode *node = item->node;
    if (node->isOfType(SoFCPathAnnotation::getClassTypeId())) {
        auto path = static_cast<SoFCPathAnnotation*>(node)->getPath();
        return path ? path->getLength() : 0;
    } else if (node->isOfType(SoGroup::getClassTypeId())) {
        int count = static_cast<SoGroup*>(node)->getNumChildren();
        if (count==1 && !item->expand)
            return 0;
        return count;
    }
    return 0;
}

int SceneModel::columnCount(const QModelIndex &) const
{
    return 2;
}

// --------------------------------------------------------

/* TRANSLATOR Gui::Dialog::DlgInspector */

DlgInspector::DlgInspector(QWidget* parent, Qt::WindowFlags fl)
  : QDialog(parent, fl), ui(new Ui_SceneInspector())
{
    ui->setupUi(this);
    setWindowTitle(tr("Scene Inspector"));

    SceneModel* model = new SceneModel(this);
    ui->treeView->setModel(model);
    ui->treeView->setRootIsDecorated(true);
}

/*
 *  Destroys the object and frees any allocated resources
 */
DlgInspector::~DlgInspector()
{
    // no need to delete child widgets, Qt does it all for us
    delete ui;
}

void DlgInspector::setDocument(Gui::Document* doc)
{
    setNodeNames(doc);

    View3DInventor* view = qobject_cast<View3DInventor*>(doc->getActiveView());
    if (view) {
        View3DInventorViewer* viewer = view->getViewer();
        setNode(viewer->getSoRenderManager()->getSceneGraph());
        SceneModel* model = static_cast<SceneModel*>(ui->treeView->model());
        Base::StateLocker lock(model->autoExpanding);
        ui->treeView->expandToDepth(4);
    }
}

void DlgInspector::setNode(SoNode* node)
{
    SceneModel* model = static_cast<SceneModel*>(ui->treeView->model());
    model->setNode(node);

    QHeaderView* header = ui->treeView->header();
#if QT_VERSION >= 0x050000
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionsMovable(false);
#else
    header->setResizeMode(0, QHeaderView::ResizeToContents);
    header->setResizeMode(1, QHeaderView::ResizeToContents);
    header->setMovable(false);
#endif
}

void DlgInspector::setNodeNames(Gui::Document* doc)
{
    std::vector<Gui::ViewProvider*> vps = doc->getViewProvidersOfType
            (Gui::ViewProviderDocumentObject::getClassTypeId());
    QHash<SoNode*, QString> nodeNames;
    for (std::vector<Gui::ViewProvider*>::iterator it = vps.begin(); it != vps.end(); ++it) {
        Gui::ViewProviderDocumentObject* vp = static_cast<Gui::ViewProviderDocumentObject*>(*it);
        App::DocumentObject* obj = vp->getObject();
        if (obj) {
            QString label = QString::fromUtf8(obj->Label.getValue());
            nodeNames[vp->getRoot()] = label;
        }

        std::vector<std::string> modes = vp->getDisplayMaskModes();
        for (std::vector<std::string>::iterator jt = modes.begin(); jt != modes.end(); ++jt) {
            SoNode* node = vp->getDisplayMaskMode(jt->c_str());
            if (node) {
                nodeNames[node] = QString::fromStdString(std::string("DisplayMode(")+*jt+")");
            }
        }
    }

    SceneModel* model = static_cast<SceneModel*>(ui->treeView->model());
    model->setNodeNames(nodeNames);
}

void DlgInspector::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        setWindowTitle(tr("Scene Inspector"));
    }
    QDialog::changeEvent(e);
}

void DlgInspector::on_refreshButton_clicked()
{
    Gui::Document* doc = Application::Instance->activeDocument();
    if (doc) {
        setNodeNames(doc);

        View3DInventor* view = qobject_cast<View3DInventor*>(doc->getActiveView());
        if (view) {
            View3DInventorViewer* viewer = view->getViewer();
            setNode(viewer->getSoRenderManager()->getSceneGraph());
            ui->treeView->expandToDepth(4);
        }
    }
}

#include "moc_SceneInspector.cpp"
