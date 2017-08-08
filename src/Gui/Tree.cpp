/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
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
# include <boost/signals.hpp>
# include <boost/bind.hpp>
# include <QAction>
# include <QActionGroup>
# include <QApplication>
# include <qcursor.h>
# include <qlayout.h>
# include <qstatusbar.h>
# include <QContextMenuEvent>
# include <QMenu>
# include <QPixmap>
# include <QTimer>
# include <QToolTip>
# include <QHeaderView>
#endif

#include <Base/Console.h>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectGroup.h>
#include <App/GeoFeatureGroupExtension.h>

#include "Tree.h"
#include "Command.h"
#include "Document.h"
#include "BitmapFactory.h"
#include "ViewProviderDocumentObject.h"
#include "MenuManager.h"
#include "Application.h"
#include "MainWindow.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"

FC_LOG_LEVEL_INIT("Tree",false,true,true);

using namespace Gui;

QPixmap*  TreeWidget::documentPixmap = 0;
const int TreeWidget::DocumentType = 1000;
const int TreeWidget::ObjectType = 1001;


/* TRANSLATOR Gui::TreeWidget */
TreeWidget::TreeWidget(QWidget* parent)
    : QTreeWidget(parent), SelectionObserver(false), contextItem(0), currentDocItem(0),fromOutside(false)
{
    this->setDragEnabled(true);
    this->setAcceptDrops(true);
    this->setDropIndicatorShown(false);
    this->setRootIsDecorated(false);

#define GET_TREEVIEW_PARAM(_name) \
    ParameterGrp::handle _name = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/TreeView")

    GET_TREEVIEW_PARAM(hGrp);
    bool sync = hGrp->GetBool("SyncSelection",false);
    this->syncSelectionAction = new QAction(this);
    this->syncSelectionAction->setCheckable(true);
    this->syncSelectionAction->setChecked(sync);
    connect(this->syncSelectionAction, SIGNAL(triggered()),
            this, SLOT(onSyncSelection()));

    this->syncViewAction = new QAction(this);
    this->syncViewAction->setCheckable(true);
    this->syncViewAction->setChecked(hGrp->GetBool("SyncView",false));
    connect(this->syncViewAction, SIGNAL(triggered()),
            this, SLOT(onSyncView()));

    this->showHiddenAction = new QAction(this);
    this->showHiddenAction->setCheckable(true);
    connect(this->showHiddenAction, SIGNAL(triggered()),
            this, SLOT(onShowHidden()));

    this->hideInTreeAction = new QAction(this);
    this->hideInTreeAction->setCheckable(true);
    connect(this->hideInTreeAction, SIGNAL(triggered()),
            this, SLOT(onHideInTree()));

    this->createGroupAction = new QAction(this);
    connect(this->createGroupAction, SIGNAL(triggered()),
            this, SLOT(onCreateGroup()));

    this->relabelObjectAction = new QAction(this);
    this->relabelObjectAction->setShortcut(Qt::Key_F2);
    connect(this->relabelObjectAction, SIGNAL(triggered()),
            this, SLOT(onRelabelObject()));

    this->finishEditingAction = new QAction(this);
    connect(this->finishEditingAction, SIGNAL(triggered()),
            this, SLOT(onFinishEditing()));

    this->skipRecomputeAction = new QAction(this);
    this->skipRecomputeAction->setCheckable(true);
    connect(this->skipRecomputeAction, SIGNAL(toggled(bool)),
            this, SLOT(onSkipRecompute(bool)));

    this->markRecomputeAction = new QAction(this);
    connect(this->markRecomputeAction, SIGNAL(triggered()),
            this, SLOT(onMarkRecompute()));

    this->selectAllInstancesAction = new QAction(this);
    connect(this->selectAllInstancesAction, SIGNAL(triggered()),
            this, SLOT(onSelectAllInstances()));

    this->selectLinkedAction = new QAction(this);
    connect(this->selectLinkedAction, SIGNAL(triggered()),
            this, SLOT(onSelectLinked()));

    this->selectLinkedFinalAction = new QAction(this);
    connect(this->selectLinkedFinalAction, SIGNAL(triggered()),
            this, SLOT(onSelectLinkedFinal()));

    this->selectAllLinksAction = new QAction(this);
    connect(this->selectAllLinksAction, SIGNAL(triggered()),
            this, SLOT(onSelectAllLinks()));

    // Setup connections
    Application::Instance->signalNewDocument.connect(boost::bind(&TreeWidget::slotNewDocument, this, _1));
    Application::Instance->signalDeleteDocument.connect(boost::bind(&TreeWidget::slotDeleteDocument, this, _1));
    Application::Instance->signalRenameDocument.connect(boost::bind(&TreeWidget::slotRenameDocument, this, _1));
    Application::Instance->signalActiveDocument.connect(boost::bind(&TreeWidget::slotActiveDocument, this, _1));
    Application::Instance->signalRelabelDocument.connect(boost::bind(&TreeWidget::slotRelabelDocument, this, _1));
    Application::Instance->signalShowHidden.connect(boost::bind(&TreeWidget::slotShowHidden, this, _1));
    
    // Gui::Document::signalChangedObject informs the App::Document property
    // change, not view provider's own property, which is what the signal below
    // for
    Application::Instance->signalChangedObject.connect(
            boost::bind(&TreeWidget::slotChangedViewObject, this, _1,_2));

    // make sure to show a horizontal scrollbar if needed
#if QT_VERSION >= 0x050000
    this->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
#else
    this->header()->setResizeMode(0, QHeaderView::ResizeToContents);
#endif
    this->header()->setStretchLastSection(false);

    // Add the first main label
    this->rootItem = new QTreeWidgetItem(this);
    this->rootItem->setFlags(Qt::ItemIsEnabled);
    this->expandItem(this->rootItem);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);
#if QT_VERSION >= 0x040200
    // causes unexpected drop events (possibly only with Qt4.1.x)
    this->setMouseTracking(true); // needed for itemEntered() to work
#endif

    this->statusTimer = new QTimer(this);

    connect(this->statusTimer, SIGNAL(timeout()),
            this, SLOT(onTestStatus()));
    connect(this, SIGNAL(itemEntered(QTreeWidgetItem*, int)),
            this, SLOT(onItemEntered(QTreeWidgetItem*)));
    connect(this, SIGNAL(itemCollapsed(QTreeWidgetItem*)),
            this, SLOT(onItemCollapsed(QTreeWidgetItem*)));
    connect(this, SIGNAL(itemExpanded(QTreeWidgetItem*)),
            this, SLOT(onItemExpanded(QTreeWidgetItem*)));
    connect(this, SIGNAL(itemSelectionChanged()),
            this, SLOT(onItemSelectionChanged()));

    setupText();
    onTestStatus();
    documentPixmap = new QPixmap(Gui::BitmapFactory().pixmap("Document"));
}

TreeWidget::~TreeWidget()
{
}

void TreeWidget::contextMenuEvent (QContextMenuEvent * e)
{
    // ask workbenches and view provider, ...
    MenuItem view;
    Gui::Application::Instance->setupContextMenu("Tree", &view);

    QMenu contextMenu;
    QMenu subMenu;
    QMenu editMenu;
    QActionGroup subMenuGroup(&subMenu);
    subMenuGroup.setExclusive(true);
    connect(&subMenuGroup, SIGNAL(triggered(QAction*)),
            this, SLOT(onActivateDocument(QAction*)));
    MenuManager::getInstance()->setupContextMenu(&view, contextMenu);

    // get the current item
    this->contextItem = itemAt(e->pos());
    contextMenu.addAction(this->syncSelectionAction);
    contextMenu.addAction(this->syncViewAction);

    if (this->contextItem && this->contextItem->type() == DocumentType) {
        if (!contextMenu.actions().isEmpty())
            contextMenu.addSeparator();
        DocumentItem* docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        showHiddenAction->setChecked(docitem->showHidden());
        contextMenu.addAction(this->showHiddenAction);
        this->skipRecomputeAction->setChecked(doc->testStatus(App::Document::SkipRecompute));
        contextMenu.addAction(this->skipRecomputeAction);
        contextMenu.addAction(this->markRecomputeAction);
        contextMenu.addAction(this->createGroupAction);
    }
    else if (this->contextItem && this->contextItem->type() == ObjectType) {
        DocumentObjectItem* objitem = static_cast<DocumentObjectItem*>
            (this->contextItem);
        if (objitem->object()->getObject()->isDerivedFrom(App::DocumentObjectGroup
            ::getClassTypeId())) {
            QList<QAction*> acts = contextMenu.actions();
            if (!acts.isEmpty()) {
                QAction* first = acts.front();
                QAction* sep = contextMenu.insertSeparator(first);
                contextMenu.insertAction(sep, this->createGroupAction);
            }
            else
                contextMenu.addAction(this->createGroupAction);
        }
        if (!contextMenu.actions().isEmpty())
            contextMenu.addSeparator();
        App::Document* doc = objitem->object()->getObject()->getDocument();
        showHiddenAction->setChecked(doc->ShowHidden.getValue());
        contextMenu.addAction(this->showHiddenAction);
        hideInTreeAction->setChecked(!objitem->object()->showInTree());
        contextMenu.addAction(this->hideInTreeAction);
        contextMenu.addAction(this->markRecomputeAction);
        contextMenu.addAction(this->relabelObjectAction);

        // if only one item is selected setup the edit menu
        if (this->selectedItems().size() == 1) {
            contextMenu.addAction(this->selectAllInstancesAction);
            if(App::GetApplication().hasLinksTo(objitem->object()->getObject()))
                contextMenu.addAction(this->selectAllLinksAction);
            if(objitem->isLink()) {
                contextMenu.addAction(this->selectLinkedAction);
                if(!objitem->isLinkFinal())
                    contextMenu.addAction(this->selectLinkedFinalAction);
            }

            objitem->object()->setupContextMenu(&editMenu, this, SLOT(onStartEditing()));
            QList<QAction*> editAct = editMenu.actions();
            if (!editAct.isEmpty()) {
                QAction* topact = contextMenu.actions().front();
                for (QList<QAction*>::iterator it = editAct.begin(); it != editAct.end(); ++it)
                    contextMenu.insertAction(topact, *it);
                QAction* first = editAct.front();
                contextMenu.setDefaultAction(first);
                if (objitem->object()->isEditing())
                    contextMenu.insertAction(topact, this->finishEditingAction);
                contextMenu.insertSeparator(topact);
            }
        }
    }

    // add a submenu to active a document if two or more exist
    std::vector<App::Document*> docs = App::GetApplication().getDocuments();
    if (docs.size() >= 2) {
        App::Document* activeDoc = App::GetApplication().getActiveDocument();
        subMenu.setTitle(tr("Activate document"));
        contextMenu.addMenu(&subMenu);
        QAction* active = 0;
        for (std::vector<App::Document*>::iterator it = docs.begin(); it != docs.end(); ++it) {
            QString label = QString::fromUtf8((*it)->Label.getValue());
            QAction* action = subMenuGroup.addAction(label);
            action->setCheckable(true);
            action->setStatusTip(tr("Activate document %1").arg(label));
            action->setData(QByteArray((*it)->getName()));
            if (*it == activeDoc) active = action;
        }

        if (active)
            active->setChecked(true);
        subMenu.addActions(subMenuGroup.actions());
    }

    if (contextMenu.actions().count() > 0)
        contextMenu.exec(QCursor::pos());
}

void TreeWidget::hideEvent(QHideEvent *ev) {
    FC_TRACE(this << " detaching selection observer");
    this->detachSelection();
    QTreeWidget::hideEvent(ev);
}

void TreeWidget::showEvent(QShowEvent *ev) {
    FC_TRACE(this << " attaching selection observer");
    this->attachSelection();
    this->syncSelection();
    QTreeWidget::showEvent(ev);
}

void TreeWidget::onCreateGroup()
{
    QString name = tr("Group");
    if (this->contextItem->type() == DocumentType) {
        DocumentItem* docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        QString cmd = QString::fromLatin1("App.getDocument(\"%1\").addObject"
                              "(\"App::DocumentObjectGroup\",\"%2\")")
                              .arg(QString::fromLatin1(doc->getName())).arg(name);
        Gui::Document* gui = Gui::Application::Instance->getDocument(doc);
        gui->openCommand("Create group");
        Gui::Command::runCommand(Gui::Command::App, cmd.toUtf8());
        gui->commitCommand();
    }
    else if (this->contextItem->type() == ObjectType) {
        DocumentObjectItem* objitem = static_cast<DocumentObjectItem*>
            (this->contextItem);
        App::DocumentObject* obj = objitem->object()->getObject();
        App::Document* doc = obj->getDocument();
        QString cmd = QString::fromLatin1("App.getDocument(\"%1\").getObject(\"%2\")"
                              ".newObject(\"App::DocumentObjectGroup\",\"%3\")")
                              .arg(QString::fromLatin1(doc->getName()))
                              .arg(QString::fromLatin1(obj->getNameInDocument()))
                              .arg(name);
        Gui::Document* gui = Gui::Application::Instance->getDocument(doc);
        gui->openCommand("Create group");
        Gui::Command::runCommand(Gui::Command::App, cmd.toUtf8());
        gui->commitCommand();
    }
}

void TreeWidget::onRelabelObject()
{
    QTreeWidgetItem* item = currentItem();
    if (item)
        editItem(item);
}

void TreeWidget::onStartEditing()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        if (this->contextItem && this->contextItem->type() == ObjectType) {
            DocumentObjectItem* objitem = static_cast<DocumentObjectItem*>
                (this->contextItem);
            int edit = action->data().toInt();
            App::DocumentObject* obj = objitem->object()->getObject();
            if (!obj) return;
            Gui::Document* doc = Gui::Application::Instance->getDocument(obj->getDocument());
            MDIView *view = doc->getActiveView();
            if (view) getMainWindow()->setActiveWindow(view);

            // Always open a transaction here doesn't make much sense because:
            // - many objects open transactions when really changing some properties
            // - this leads to certain inconsistencies with the doubleClicked() method
            // So, only the view provider class should decide what to do
#if 0
            // open a transaction before starting edit mode
            std::string cmd("Edit ");
            cmd += obj->Label.getValue();
            doc->openCommand(cmd.c_str());
            bool ok = doc->setEdit(objitem->object(), edit);
            if (!ok) doc->abortCommand();
#else
            doc->setEdit(objitem->object(), edit);
#endif
        }
    }
}

void TreeWidget::onFinishEditing()
{
    if (this->contextItem && this->contextItem->type() == ObjectType) {
        DocumentObjectItem* objitem = static_cast<DocumentObjectItem*>
            (this->contextItem);
        App::DocumentObject* obj = objitem->object()->getObject();
        if (!obj) return;
        Gui::Document* doc = Gui::Application::Instance->getDocument(obj->getDocument());
        doc->commitCommand();
        doc->resetEdit();
        doc->getDocument()->recompute();
    }
}

void TreeWidget::onSkipRecompute(bool on)
{
    // if a document item is selected then touch all objects
    if (this->contextItem && this->contextItem->type() == DocumentType) {
        DocumentItem* docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        doc->setStatus(App::Document::SkipRecompute, on);
    }
}

void TreeWidget::onMarkRecompute()
{
    // if a document item is selected then touch all objects
    if (this->contextItem && this->contextItem->type() == DocumentType) {
        DocumentItem* docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        std::vector<App::DocumentObject*> obj = doc->getObjects();
        for (std::vector<App::DocumentObject*>::iterator it = obj.begin(); it != obj.end(); ++it)
            (*it)->touch();
    }
    // mark all selected objects
    else {
        QList<QTreeWidgetItem*> items = this->selectedItems();
        for (QList<QTreeWidgetItem*>::iterator it = items.begin(); it != items.end(); ++it) {
            if ((*it)->type() == ObjectType) {
                DocumentObjectItem* objitem = static_cast<DocumentObjectItem*>(*it);
                App::DocumentObject* obj = objitem->object()->getObject();
                obj->touch();
            }
        }
    }
}

DocumentItem *TreeWidget::getDocumentItem(const Gui::Document *doc) const {
    auto it = DocumentMap.find(doc);
    if(it != DocumentMap.end())
        return it->second;
    return 0;
}

void TreeWidget::onSelectLinked()
{
    if (!this->contextItem || this->contextItem->type() != ObjectType) return;
    auto item = static_cast<DocumentObjectItem*>(this->contextItem);
    auto docItem = getDocumentItem(item->object()->getDocument());
    if(docItem)
        docItem->selectLinkedItem(item,false);
}

void TreeWidget::onSelectLinkedFinal()
{
    if (!this->contextItem || this->contextItem->type() != ObjectType) return;
    auto item = static_cast<DocumentObjectItem*>(this->contextItem);
    auto docItem = getDocumentItem(item->object()->getDocument());
    if(docItem)
        docItem->selectLinkedItem(item,true);
}

void TreeWidget::onSelectAllLinks()
{
    if (!this->contextItem || this->contextItem->type() != ObjectType) return;
    auto item = static_cast<DocumentObjectItem*>(this->contextItem);
    auto docItem = getDocumentItem(item->object()->getDocument());
    if(docItem) {
        item->setSelected(false);
        docItem->updateSelection();
    }
    for(auto link: App::GetApplication().getLinksTo(item->object()->getObject(),true)) {
        if(!link || !link->getNameInDocument()) 
            continue;
        auto vp = Application::Instance->getViewProvider(link);
        if(!vp || !vp->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId()))
            continue;
        for(auto &v : DocumentMap)
            v.second->selectAllInstances(*static_cast<ViewProviderDocumentObject*>(vp));
    }
}

void TreeWidget::onSelectAllInstances()
{
    if (!this->contextItem || this->contextItem->type() != ObjectType) return;
    auto item = static_cast<DocumentObjectItem*>(this->contextItem);
    for(const auto &v : DocumentMap) 
        v.second->selectAllInstances(*item->object());
}

void TreeWidget::onActivateDocument(QAction* active)
{
    // activate the specified document
    QByteArray docname = active->data().toByteArray();
    Gui::Document* doc = Application::Instance->getDocument((const char*)docname);
    if (!doc) return;
    MDIView *view = doc->getActiveView();
    if (!view) return;
    getMainWindow()->setActiveWindow(view);
}

Qt::DropActions TreeWidget::supportedDropActions () const
{
    return QTreeWidget::supportedDropActions();
}

bool TreeWidget::event(QEvent *e)
{
#if 0
    if (e->type() == QEvent::ShortcutOverride) {
        QKeyEvent* ke = static_cast<QKeyEvent *>(e);
        switch (ke->key()) {
            case Qt::Key_Delete:
                ke->accept();
        }
    }
#endif
    return QTreeWidget::event(e);
}

void TreeWidget::keyPressEvent(QKeyEvent *event)
{
#if 0
    if (event && event->matches(QKeySequence::Delete)) {
        event->ignore();
    }
#endif
    QTreeWidget::keyPressEvent(event);
}

void TreeWidget::mouseDoubleClickEvent (QMouseEvent * event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if (!item) return;
    if (item->type() == TreeWidget::DocumentType) {
        //QTreeWidget::mouseDoubleClickEvent(event);
        const Gui::Document* doc = static_cast<DocumentItem*>(item)->document();
        if (!doc) return;
        MDIView *view = doc->getActiveView();
        if (!view) return;
        getMainWindow()->setActiveWindow(view);
    }
    else if (item->type() == TreeWidget::ObjectType) {
        DocumentObjectItem* objitem = static_cast<DocumentObjectItem*>(item);
        App::DocumentObject* obj = objitem->object()->getObject();
        Gui::Document* doc = Gui::Application::Instance->getDocument(obj->getDocument());
        MDIView *view = doc->getActiveView();
        if (view) getMainWindow()->setActiveWindow(view);
        if (!objitem->object()->doubleClicked())
            QTreeWidget::mouseDoubleClickEvent(event);
    }
}

void TreeWidget::startDrag(Qt::DropActions supportedActions)
{
    QTreeWidget::startDrag(supportedActions);
}

QMimeData * TreeWidget::mimeData (const QList<QTreeWidgetItem *> items) const
{
    // all selected items must reference an object from the same document
    App::Document* doc=0;
    for (QList<QTreeWidgetItem *>::ConstIterator it = items.begin(); it != items.end(); ++it) {
        if ((*it)->type() != TreeWidget::ObjectType)
            return 0;
        App::DocumentObject* obj = static_cast<DocumentObjectItem *>(*it)->object()->getObject();
        if (!doc)
            doc = obj->getDocument();
        else if (doc != obj->getDocument())
            return 0;
    }
    return QTreeWidget::mimeData(items);
}

bool TreeWidget::dropMimeData(QTreeWidgetItem *parent, int index,
                              const QMimeData *data, Qt::DropAction action)
{
    return QTreeWidget::dropMimeData(parent, index, data, action);
}

void TreeWidget::dragEnterEvent(QDragEnterEvent * event)
{
    QTreeWidget::dragEnterEvent(event);
}

void TreeWidget::dragLeaveEvent(QDragLeaveEvent * event)
{
    QTreeWidget::dragLeaveEvent(event);
}

void TreeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    QTreeWidget::dragMoveEvent(event);
    if (!event->isAccepted())
        return;

    QTreeWidgetItem* targetitem = itemAt(event->pos());
    if (!targetitem || this->isItemSelected(targetitem)) {
        event->ignore();
    }
    else if (targetitem->type() == TreeWidget::DocumentType) {
        QList<QModelIndex> idxs = selectedIndexes();
        App::Document* doc = static_cast<DocumentItem*>(targetitem)->
            document()->getDocument();
        for (QList<QModelIndex>::Iterator it = idxs.begin(); it != idxs.end(); ++it) {
            QTreeWidgetItem* item = itemFromIndex(*it);
            if (item->type() != TreeWidget::ObjectType) {
                event->ignore();
                return;
            }
            App::DocumentObject* obj = static_cast<DocumentObjectItem*>(item)->
                object()->getObject();
            if (doc != obj->getDocument()) {
                event->ignore();
                return;
            }
        }
    }
    else if (targetitem->type() == TreeWidget::ObjectType) {

        DocumentObjectItem* targetItemObj = static_cast<DocumentObjectItem*>(targetitem);
        Gui::ViewProviderDocumentObject* vp = targetItemObj->object();

        if (!vp->canDropObjects()) {
            event->ignore();
        }

        QList<QModelIndex> idxs = selectedIndexes();
        for (QList<QModelIndex>::Iterator it = idxs.begin(); it != idxs.end(); ++it) {
            QTreeWidgetItem* ti = itemFromIndex(*it);
            if (ti->type() != TreeWidget::ObjectType) {
                event->ignore();
                return;
            }
            auto item = static_cast<DocumentObjectItem*>(ti);

            // To avoid a cylic dependency it must be made sure to not allow to
            // drag'n'drop a tree item onto a child or grandchild item of it.
            if (static_cast<DocumentObjectItem*>(targetitem)->isChildOfItem(item)) {
                event->ignore();
                return;
            }

            // if the item is already a child of the target item there is nothing to do
            if (item->parent() == targetitem) {
                event->ignore();
                return;
            }

            auto obj = item->object()->getObject();

            std::ostringstream str;
            auto owner = item->getFullSubName(str);

            // let the view provider decide to accept the object or ignore it
            if (!vp->canDropObjectEx(obj,owner,str.str().c_str())) {
                event->ignore();
                return;
            }
        }
    }
    else {
        event->ignore();
    }
}

void TreeWidget::dropEvent(QDropEvent *event)
{
    //FIXME: This should actually be done inside dropMimeData

    QTreeWidgetItem* targetitem = itemAt(event->pos());
    // not dropped onto an item
    if (!targetitem)
        return;
    // one of the source items is also the destination item, that's not allowed
    if (this->isItemSelected(targetitem))
        return;

    // filter out the selected items we cannot handle
    QList<QTreeWidgetItem*> items;
    QList<QModelIndex> idxs = selectedIndexes();
    for (QList<QModelIndex>::Iterator it = idxs.begin(); it != idxs.end(); ++it) {
        // ignore child elements if the parent is selected
        QModelIndex parent = (*it).parent();
        if (idxs.contains(parent))
            continue;
        QTreeWidgetItem* item = itemFromIndex(*it);
        if (item == targetitem)
            continue;
        if (item->parent() == targetitem)
            continue;
        items.push_back(item);
    }

    if (items.isEmpty())
        return; // nothing needs to be done

    if (targetitem->type() == TreeWidget::ObjectType) {
        // add object to group
        DocumentObjectItem* targetItemObj = static_cast<DocumentObjectItem*>(targetitem);
        Gui::ViewProviderDocumentObject* vp = targetItemObj->object();
        if (!vp->canDropObjects()) {
            return; // no group like object
        }

        bool dropOnly = QApplication::keyboardModifiers()== Qt::ControlModifier;
        if(!dropOnly) {
            // check if items can be dragged
            for(auto ti : items) {
                auto item = static_cast<DocumentObjectItem*>(ti);
                auto parentItem = item->getParentItem();
                if(!parentItem || !vp->canDragAndDropObject(item->object()->getObject()))
                    continue;
                if(!parentItem->object()->canDragObjects() || 
                   !parentItem->object()->canDragObject(item->object()->getObject()))
                {
                    FC_WARN("'" << item->object()->getObject()->getNameInDocument() << 
                            "' cannot be dragged out of '" << 
                            parentItem->object()->getObject()->getNameInDocument() << "'");
                    return;
                }
            }
        }

        // Open command
        Gui::Document* gui = vp->getDocument();
        gui->openCommand("Drag object");
        for (QList<QTreeWidgetItem*>::Iterator it = items.begin(); it != items.end(); ++it) {
            auto item = static_cast<DocumentObjectItem*>(*it);
            Gui::ViewProviderDocumentObject* vpc = item->object();
            App::DocumentObject* obj = vpc->getObject();

            std::ostringstream str;
            auto owner = item->getFullSubName(str);
            std::string subname = str.str();

            if(!dropOnly && vp->canDragAndDropObject(obj)) {
                // does this have a parent object
                QTreeWidgetItem* parent = (*it)->parent();
                if (parent && parent->type() == TreeWidget::ObjectType) {
                    Gui::ViewProvider* vpp = static_cast<DocumentObjectItem *>(parent)->object();
                    vpp->dragObject(obj);
                    owner = 0;
                    subname.clear();
                }
            }

            // now add the object to the target object
            vp->dropObjectEx(obj,owner,subname.c_str());
        }
        gui->commitCommand();
    }
    else if (targetitem->type() == TreeWidget::DocumentType) {
        // check if items can be dragged
        for(auto ti : items) {
            auto item = static_cast<DocumentObjectItem*>(ti);
            auto parentItem = item->getParentItem();
            if(!parentItem) 
                continue;
            if(!parentItem->object()->canDragObjects() || 
               !parentItem->object()->canDragObject(item->object()->getObject()))
            {
                FC_WARN("'" << item->object()->getObject()->getNameInDocument() << 
                        "' cannot be dragged out of '" << 
                        parentItem->object()->getObject()->getNameInDocument() << "'");
                return;
            }
        }
        // Open command
        App::Document* doc = static_cast<DocumentItem*>(targetitem)->document()->getDocument();
        Gui::Document* gui = Gui::Application::Instance->getDocument(doc);
        gui->openCommand("Move object");
        for (QList<QTreeWidgetItem*>::Iterator it = items.begin(); it != items.end(); ++it) {
            Gui::ViewProviderDocumentObject* vpc = static_cast<DocumentObjectItem*>(*it)->object();
            App::DocumentObject* obj = vpc->getObject();

            // does this have a parent object
            QTreeWidgetItem* parent = (*it)->parent();
            if (parent && parent->type() == TreeWidget::ObjectType) {
                Gui::ViewProvider* vpp = static_cast<DocumentObjectItem *>(parent)->object();
                vpp->dragObject(obj);
            }

            //make sure it is not part of a geofeaturegroup anymore. When this has happen we need to handle 
            //all removed objects
            auto grp = App::GeoFeatureGroupExtension::getGroupOfObject(obj);
            if(grp) {
                grp->getExtensionByType<App::GeoFeatureGroupExtension>()->removeObject(obj);
                // children view provider maintainace is now handled by
                // Gui::Document::handleChildren3D()
            }
        }
        gui->commitCommand();
    }
}

void TreeWidget::drawRow(QPainter *painter, const QStyleOptionViewItem &options, const QModelIndex &index) const
{
    QTreeWidget::drawRow(painter, options, index);
    // Set the text and highlighted text color of a hidden object to a dark
    //QTreeWidgetItem * item = itemFromIndex(index);
    //if (item->type() == ObjectType && !(static_cast<DocumentObjectItem*>(item)->previousStatus & 1)) {
    //    QStyleOptionViewItem opt(options);
    //    opt.state ^= QStyle::State_Enabled;
    //    QColor c = opt.palette.color(QPalette::Inactive, QPalette::Dark);
    //    opt.palette.setColor(QPalette::Inactive, QPalette::Text, c);
    //    opt.palette.setColor(QPalette::Inactive, QPalette::HighlightedText, c);
    //    QTreeWidget::drawRow(painter, opt, index);
    //}
    //else {
    //    QTreeWidget::drawRow(painter, options, index);
    //}
}

void TreeWidget::slotNewDocument(const Gui::Document& Doc)
{
    DocumentItem* item = new DocumentItem(&Doc, this->rootItem);
    this->expandItem(item);
    item->setIcon(0, *documentPixmap);
    item->setText(0, QString::fromUtf8(Doc.getDocument()->Label.getValue()));
    DocumentMap[ &Doc ] = item;
}

void TreeWidget::slotDeleteDocument(const Gui::Document& Doc)
{
    std::map<const Gui::Document*, DocumentItem*>::iterator it = DocumentMap.find(&Doc);
    if (it != DocumentMap.end()) {
        for(auto &v : DocumentMap)
            v.second->onDeleteDocument(it->second);
        this->rootItem->takeChild(this->rootItem->indexOfChild(it->second));
        delete it->second;
        DocumentMap.erase(it);
    }
}

void TreeWidget::slotRenameDocument(const Gui::Document& Doc)
{
    // do nothing here
    Q_UNUSED(Doc); 
}

void TreeWidget::slotChangedViewObject(const Gui::ViewProvider& vp, const App::Property &prop)
{
    if(!vp.isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) 
        return;
    const auto &vpd = static_cast<const ViewProviderDocumentObject&>(vp);
    if(&prop == &vpd.ShowInTree) {
        for(auto &v : DocumentMap) 
            v.second->setItemVisibility(vpd);
    }else{
        for(auto &v : DocumentMap) 
            v.second->checkRemoveChildrenFromRoot(vpd);
    }
}

void TreeWidget::slotShowHidden(const Gui::Document& Doc)
{
    std::map<const Gui::Document*, DocumentItem*>::iterator it = DocumentMap.find(&Doc);
    if (it != DocumentMap.end())
        it->second->updateItemsVisibility(it->second,it->second->showHidden());
}

void TreeWidget::slotRelabelDocument(const Gui::Document& Doc)
{
    std::map<const Gui::Document*, DocumentItem*>::iterator it = DocumentMap.find(&Doc);
    if (it != DocumentMap.end()) {
        it->second->setText(0, QString::fromUtf8(Doc.getDocument()->Label.getValue()));
    }
}

void TreeWidget::slotActiveDocument(const Gui::Document& Doc)
{
    std::map<const Gui::Document*, DocumentItem*>::iterator jt = DocumentMap.find(&Doc);
    if (jt == DocumentMap.end())
        return; // signal is emitted before the item gets created
    for (std::map<const Gui::Document*, DocumentItem*>::iterator it = DocumentMap.begin();
         it != DocumentMap.end(); ++it)
    {
        QFont f = it->second->font(0);
        f.setBold(it == jt);
        it->second->setFont(0,f);
    }
}

void TreeWidget::onTestStatus(void)
{
    if (isVisible()) {
        std::map<const Gui::Document*,DocumentItem*>::iterator pos;
        for (pos = DocumentMap.begin();pos!=DocumentMap.end();++pos) {
            pos->second->testStatus();
        }
    }

    this->statusTimer->setSingleShot(true);
    this->statusTimer->start(300);
}

void TreeWidget::onItemEntered(QTreeWidgetItem * item)
{
    // object item selected
    if (item && item->type() == TreeWidget::ObjectType) {
        DocumentObjectItem* obj = static_cast<DocumentObjectItem*>(item);
        obj->displayStatusInfo();
    }
}

void TreeWidget::onItemCollapsed(QTreeWidgetItem * item)
{
    // object item collapsed
    if (item && item->type() == TreeWidget::ObjectType) {
        DocumentObjectItem* obj = static_cast<DocumentObjectItem*>(item);
        obj->setExpandedStatus(false);
    }
}

void TreeWidget::onItemExpanded(QTreeWidgetItem * item)
{
    // object item expanded
    if (item && item->type() == TreeWidget::ObjectType) {
        DocumentObjectItem* obj = static_cast<DocumentObjectItem*>(item);
        auto it = DocumentMap.find(obj->object()->getDocument());
        if(it==DocumentMap.end()) 
            Base::Console().Warning("DocumentItem::onItemExpanded: cannot find object document\n");
        else {
            it->second->populateItem(obj);
            obj->setExpandedStatus(true);
        }
    }
}

void TreeWidget::scrollItemToTop(Gui::Document* doc)
{
    if(!isConnectionAttached()) 
        return;

    std::map<const Gui::Document*,DocumentItem*>::iterator it;
    it = DocumentMap.find(doc);
    if (it != DocumentMap.end()) {
        if(!syncSelectionAction->isChecked()) {
            bool lock = this->blockConnection(true);
            it->second->selectItems(true);
            this->blockConnection(lock);
            return;
        }
        DocumentItem* root = it->second;
        QTreeWidgetItemIterator it(root, QTreeWidgetItemIterator::Selected);
        for (; *it; ++it) {
            if ((*it)->type() == TreeWidget::ObjectType) {
                this->scrollToItem(*it, QAbstractItemView::PositionAtTop);
                break;
            }
        }
    }
}

void TreeWidget::setupText() {
    this->headerItem()->setText(0, tr("Labels & Attributes"));
    this->rootItem->setText(0, tr("Application"));

    this->syncSelectionAction->setText(tr("Sync selection"));
    this->syncSelectionAction->setStatusTip(tr("Auto expand item when selected in 3D view"));

    this->syncViewAction->setText(tr("Sync view"));
    this->syncViewAction->setStatusTip(tr("Auto switch to the 3D view containing the selected item"));

    this->showHiddenAction->setText(tr("Show hidden items"));
    this->showHiddenAction->setStatusTip(tr("Show hidden tree view items"));

    this->hideInTreeAction->setText(tr("Hide item"));
    this->hideInTreeAction->setStatusTip(tr("Hide the item in tree"));

    this->createGroupAction->setText(tr("Create group..."));
    this->createGroupAction->setStatusTip(tr("Create a group"));

    this->relabelObjectAction->setText(tr("Rename"));
    this->relabelObjectAction->setStatusTip(tr("Rename object"));

    this->finishEditingAction->setText(tr("Finish editing"));
    this->finishEditingAction->setStatusTip(tr("Finish editing object"));

    this->skipRecomputeAction->setText(tr("Skip recomputes"));
    this->skipRecomputeAction->setStatusTip(tr("Enable or disable recomputations of document"));

    this->markRecomputeAction->setText(tr("Mark to recompute"));
    this->markRecomputeAction->setStatusTip(tr("Mark this object to be recomputed"));

    this->selectAllInstancesAction->setText(tr("Select all instances"));
    this->selectAllInstancesAction->setStatusTip(tr("Select all instances of this object with different parents"));

    this->selectLinkedAction->setText(tr("Select linked object"));
    this->selectLinkedAction->setStatusTip(tr("Select the object that is linked by this item"));

    this->selectLinkedFinalAction->setText(tr("Select final linked object"));
    this->selectLinkedFinalAction->setStatusTip(tr("Select the deepest object that is linked by this item"));

    this->selectAllLinksAction->setText(tr("Select all links"));
    this->selectAllLinksAction->setStatusTip(tr("Select all links to this object"));
}

void TreeWidget::onSyncSelection() {
    GET_TREEVIEW_PARAM(hGrp);
    hGrp->SetBool("SyncSelection",syncSelectionAction->isChecked());
}

void TreeWidget::onSyncView() {
    GET_TREEVIEW_PARAM(hGrp);
    hGrp->SetBool("SyncView",syncViewAction->isChecked());
}

void TreeWidget::syncView() {
    if(currentDocItem && syncViewAction->isChecked()) {
        MDIView *view = currentDocItem->document()->getActiveView();
        if (view) getMainWindow()->setActiveWindow(view);
    }
}

void TreeWidget::onShowHidden() {
    if (!this->contextItem) return;
    DocumentItem *docItem = nullptr;
    if(this->contextItem->type() == DocumentType)
        docItem = static_cast<DocumentItem*>(contextItem);
    else if(this->contextItem->type() == ObjectType)
        docItem = getDocumentItem(
            static_cast<DocumentObjectItem*>(contextItem)->object()->getDocument());
    if(docItem)
        docItem->setShowHidden(showHiddenAction->isChecked());
}

void TreeWidget::onHideInTree() {
    if (this->contextItem && this->contextItem->type() == ObjectType) {
        auto item = static_cast<DocumentObjectItem*>(contextItem);
        item->object()->ShowInTree.setValue(!hideInTreeAction->isChecked());
    }
}


void TreeWidget::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        setupText();

    QTreeWidget::changeEvent(e);
}

void TreeWidget::onItemSelectionChanged ()
{
    if (this->isConnectionAttached() && this->isConnectionBlocked())
        return;

    // block tmp. the connection to avoid to notify us ourself
    bool lock = this->blockConnection(true);
    std::map<const Gui::Document*,DocumentItem*>::iterator pos;
    for (pos = DocumentMap.begin();pos!=DocumentMap.end();++pos) {
        currentDocItem = pos->second;
        pos->second->updateSelection(pos->second);
        currentDocItem = 0;
    }
    this->blockConnection(lock);
}

void TreeWidget::syncSelection(const char *pDocName) {
    if(this->isConnectionBlocked()) {
        FC_TRACE("connection blocked");
        return;
    }
    if (!pDocName || *pDocName==0) {
        if(Selection().hasSelection()) {
            for(auto &v : DocumentMap) {
                bool lock = this->blockConnection(true);
                v.second->selectItems(syncSelectionAction->isChecked());
                this->blockConnection(lock);
            }
        }else{
            for(auto &v : DocumentMap)
                v.second->clearSelection();
        }
        return;
    }
    Gui::Document* pDoc = Application::Instance->getDocument(pDocName);
    std::map<const Gui::Document*, DocumentItem*>::iterator it;
    it = DocumentMap.find(pDoc);
    if (it != DocumentMap.end()) {
        if(Selection().hasSelection()) {
            bool lock = this->blockConnection(true);
            it->second->selectItems(syncSelectionAction->isChecked());
            this->blockConnection(lock);
        }else
            it->second->clearSelection();
    }
}

void TreeWidget::onSelectionChanged(const SelectionChanges& msg)
{
    switch (msg.Type)
    {
    case SelectionChanges::AddSelection:
    case SelectionChanges::RmvSelection:
    case SelectionChanges::SetSelection:
    case SelectionChanges::ClrSelection:
        syncSelection(msg.pDocName);
        break;
    default:
        break;
    }
}

// ----------------------------------------------------------------------------

/* TRANSLATOR Gui::TreeDockWidget */
TreeDockWidget::TreeDockWidget(Gui::Document* pcDocument,QWidget *parent)
  : DockWindow(pcDocument,parent)
{
    setWindowTitle(tr("Tree view"));
    this->treeWidget = new TreeWidget(this);
    this->treeWidget->setRootIsDecorated(false);
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/TreeView");
    this->treeWidget->setIndentation(hGrp->GetInt("Indentation", this->treeWidget->indentation()));

    QGridLayout* pLayout = new QGridLayout(this);
    pLayout->setSpacing(0);
    pLayout->setMargin (0);
    pLayout->addWidget(this->treeWidget, 0, 0 );
}

TreeDockWidget::~TreeDockWidget()
{
}

// ---------------------------------------------------------------------------

typedef std::set<DocumentObjectItem*> DocumentObjectItems;

class Gui::DocumentObjectData {
public:
    DocumentItem &docItem;
    DocumentObjectItems items;
    ViewProviderDocumentObject *viewObject;
    DocumentObjectItem *rootItem;
    std::vector<App::DocumentObject*> children;
    bool removeChildrenFromRoot;
    std::string label;

    typedef boost::BOOST_SIGNALS_NAMESPACE::connection Connection;

    Connection connectIcon;
    Connection connectTool;
    Connection connectStat;

    DocumentObjectData(DocumentItem &docItem, ViewProviderDocumentObject* vpd)
        : docItem(docItem),viewObject(vpd),rootItem(0)
    {
        // Setup connections
        connectIcon = viewObject->signalChangeIcon.connect(
                boost::bind(&DocumentObjectData::slotChangeIcon, this));
        connectTool = viewObject->signalChangeToolTip.connect(
                boost::bind(&DocumentObjectData::slotChangeToolTip, this, _1));
        connectStat = viewObject->signalChangeStatusTip.connect(
                boost::bind(&DocumentObjectData::slotChangeStatusTip, this, _1));

        removeChildrenFromRoot = viewObject->canRemoveChildrenFromRoot();
        children = viewObject->claimChildren();
        label = viewObject->getObject()->Label.getValue();
    }

    void testStatus(bool resetStatus = false) {
        QIcon icon,icon2;
        for(auto item : items)
            item->testStatus(resetStatus,icon,icon2);
    }

    void slotChangeIcon() {
        testStatus(true);
    }

    void slotChangeToolTip(const QString& tip) {
        for(auto item : items)
            item->setToolTip(0, tip);
    }

    void slotChangeStatusTip(const QString& tip) {
        for(auto item : items)
            item->setStatusTip(0, tip);
    }
};

// ----------------------------------------------------------------------------

DocumentItem::DocumentItem(const Gui::Document* doc, QTreeWidgetItem * parent)
    : QTreeWidgetItem(parent, TreeWidget::DocumentType), pDocument(doc)
{
    // Setup connections
    connectNewObject = doc->signalNewObject.connect(boost::bind(&DocumentItem::slotNewObject, this, _1));
    connectDelObject = doc->signalDeletedObject.connect(boost::bind(&DocumentItem::slotDeleteObject, this, _1, true));
    connectChgObject = doc->signalChangedObject.connect(boost::bind(&DocumentItem::slotChangeObject, this, _1, true));
    connectRenObject = doc->signalRelabelObject.connect(boost::bind(&DocumentItem::slotRenameObject, this, _1));
    connectActObject = doc->signalActivatedObject.connect(boost::bind(&DocumentItem::slotActiveObject, this, _1));
    connectEdtObject = doc->signalInEdit.connect(boost::bind(&DocumentItem::slotInEdit, this, _1));
    connectResObject = doc->signalResetEdit.connect(boost::bind(&DocumentItem::slotResetEdit, this, _1));
    connectHltObject = doc->signalHighlightObject.connect(boost::bind(&DocumentItem::slotHighlightObject, this, _1,_2,_3));
    connectExpObject = doc->signalExpandObject.connect(boost::bind(&DocumentItem::slotExpandObject, this, _1,_2));

    setFlags(Qt::ItemIsEnabled/*|Qt::ItemIsEditable*/);
}

DocumentItem::~DocumentItem()
{
    connectNewObject.disconnect();
    connectDelObject.disconnect();
    connectChgObject.disconnect();
    connectRenObject.disconnect();
    connectActObject.disconnect();
    connectEdtObject.disconnect();
    connectResObject.disconnect();
    connectHltObject.disconnect();
    connectExpObject.disconnect();
}

TreeWidget *DocumentItem::getTree() {
    return static_cast<TreeWidget*>(treeWidget());
}

#define FOREACH_ITEM(_item, _obj) \
    auto _it = ObjectMap.end();\
    if(_obj.getObject() && _obj.getObject()->getNameInDocument())\
        _it = ObjectMap.find(_obj.getObject());\
    if(_it != ObjectMap.end()) {\
        for(auto _item : _it->second->items) {

#define FOREACH_ITEM_ALL(_item) \
    for(auto _v : ObjectMap) {\
        for(auto _item : _v.second->items) {

#define END_FOREACH_ITEM }}


void DocumentItem::slotInEdit(const Gui::ViewProviderDocumentObject& v)
{
    FOREACH_ITEM(item,v)
        item->setBackgroundColor(0,Qt::yellow);
    END_FOREACH_ITEM
}

void DocumentItem::slotResetEdit(const Gui::ViewProviderDocumentObject& v)
{
    FOREACH_ITEM(item,v)
        item->setData(0, Qt::BackgroundColorRole,QVariant());
    END_FOREACH_ITEM
}

void DocumentItem::slotNewObject(const Gui::ViewProviderDocumentObject& obj) {
    createNewItem(obj);
}

bool DocumentItem::createNewItem(const Gui::ViewProviderDocumentObject& obj,
            QTreeWidgetItem *parent, int index, DocumentObjectDataPtr data)
{
    const char *name;
    if (!obj.getObject() || !(name=obj.getObject()->getNameInDocument())) 
        return false;

    if(!data) {
        auto &pdata = ObjectMap[obj.getObject()];
        if(!pdata) {
            pdata = std::make_shared<DocumentObjectData>(*this,
                    const_cast<ViewProviderDocumentObject*>(&obj));
        }else if(pdata->rootItem && parent==NULL) {
            Base::Console().Warning("DocumentItem::slotNewObject: Cannot add view provider twice.\n");
            return false;
        }
        data = pdata;
    }
    std::string displayName = obj.getObject()->Label.getValue();
    std::string objectName = obj.getObject()->getNameInDocument();
    DocumentObjectItem* item = new DocumentObjectItem(data);
    if(!parent || parent==this) {
        parent = this;
        data->rootItem = item;
    }
    if(index<0)
        parent->addChild(item);
    else
        parent->insertChild(index,item);
    item->setIcon(0, obj.getIcon());
    item->setText(0, QString::fromUtf8(displayName.c_str()));
    item->setHidden(!obj.showInTree() && !showHidden());
    populateItem(item);
    return true;
}

void DocumentItem::onDeleteDocument(DocumentItem *docItem) {
    if(docItem == this) return;
    for(auto it=ObjectMap.begin(),itNext=it;it!=ObjectMap.end();it=itNext) {
        ++itNext;
        auto data = it->second;
        if(&data->docItem != docItem) continue;
        slotDeleteObject(*data->viewObject,false);
    }
}

ViewProviderDocumentObject *DocumentItem::getViewProvider(App::DocumentObject *obj) {
    // Note: It is possible that we receive an invalid pointer from
    // claimChildren(), e.g. if multiple properties were changed in
    // a transaction and slotChangedObject() is triggered by one
    // property being reset before the invalid pointer has been
    // removed from another. Currently this happens for
    // PartDesign::Body when cancelling a new feature in the dialog.
    // First the new feature is deleted, then the Tip property is
    // reset, but claimChildren() accesses the Model property which
    // still contains the pointer to the deleted feature
    //
    // return obj && obj->getNameInDocument() && pDocument->isIn(obj);
    //
    // TODO: is the above isIn() check still necessary? Will
    // getNameInDocument() check be sufficient?


    if(!obj || !obj->getNameInDocument()) return 0;
    ViewProvider *vp;
    if(obj->getDocument() == pDocument->getDocument()) 
        vp = pDocument->getViewProvider(obj);
    else 
        vp = Application::Instance->getViewProvider(obj);
    if(!vp || !vp->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId()))
        return 0;
    return static_cast<ViewProviderDocumentObject*>(vp);
}

void DocumentItem::slotDeleteObject(const Gui::ViewProviderDocumentObject& view, bool broadcast)
{
    if(broadcast) {
        for(auto &v : getTree()->DocumentMap)
            v.second->slotDeleteObject(view,false);
        return;
    }

    auto it = ObjectMap.find(view.getObject());
    if(it == ObjectMap.end() || it->second->items.empty()) return;
    auto &items = it->second->items;
    for(auto cit=items.begin(),citNext=cit;cit!=items.end();cit=citNext) {
        ++citNext;
        delete *cit;
    }
    
    // Check for any child of the deleted object that is not in the tree, and put it
    // under document item.
    for(auto child : it->second->children) {
        auto cit = ObjectMap.find(child);
        if(cit==ObjectMap.end() || cit->second->items.empty()) {
            auto vpd = getViewProvider(child);
            if(!vpd) continue;
            createNewItem(*vpd);
        }else {
            auto childItem = *cit->second->items.begin();
            if(childItem->requiredAtRoot(false))
                createNewItem(*childItem->object(),this,-1,childItem->myData);
        }

    }

    if(items.empty())
        ObjectMap.erase(it);
}

void DocumentItem::populateItem(DocumentObjectItem *item, bool refresh) {
    if(item->populated && !refresh) return;

    // Lazy loading policy: We will create an item for each children object if
    // a) the item is expanded, or b) there is at least one free child, i.e.
    // child originally located at root.

    item->setChildIndicatorPolicy(item->myData->children.empty()?
            QTreeWidgetItem::DontShowIndicator:QTreeWidgetItem::ShowIndicator);

    if(!item->populated && !item->isExpanded()) {
        bool doPopulate = false;
        for(auto child : item->myData->children) {
            auto it = ObjectMap.find(child);
            if(it == ObjectMap.end() || it->second->items.empty()) {
                auto vp = getViewProvider(child);
                if(!vp) continue;
                doPopulate = true;
                break;
            }
            if(item->myData->removeChildrenFromRoot) {
                if(it->second->rootItem) {
                    doPopulate = true;
                    break;
                }
            }
        }
        if(!doPopulate) return;
    }
    item->populated = true;

    int i=-1;
    // iterate through the claimed children, and try to synchronize them with the 
    // children tree item with the same order of apperance. 
    for(auto child : item->myData->children) {

        ++i; // the current index of the claimed child

        bool found = false;
        for(int j=0,count=item->childCount();j<count;++j) {
            QTreeWidgetItem *ci = item->child(j);
            if(ci->type() != TreeWidget::ObjectType) continue;
            DocumentObjectItem *childItem = static_cast<DocumentObjectItem*>(ci);
            if(childItem->object()->getObject() != child) continue;
            found = true;
            if(j!=i) { // fix index if it is changed
                item->removeChild(ci);
                item->insertChild(i,ci);
            }

            // Check if the item just changed its policy of whether to remove
            // children item from the root. 
            if(item->myData->removeChildrenFromRoot) {
                if(childItem->myData->rootItem) {
                    assert(childItem != childItem->myData->rootItem);
                    delete childItem->myData->rootItem;
                }
            }else if(childItem->requiredAtRoot())
                createNewItem(*childItem->object(),this,-1,childItem->myData);
            break;
        }
        if(found) continue;

        // This algo will be recursively applied to newly created child items
        // through slotNewObject -> populateItem

        auto it = ObjectMap.find(child);
        if(it==ObjectMap.end() || it->second->items.empty()) {
            auto vp = getViewProvider(child);
            if(!vp || !createNewItem(*vp,item,i,it==ObjectMap.end()?DocumentObjectDataPtr():it->second))
                --i;
            continue;
        }

        if(!item->myData->removeChildrenFromRoot || !it->second->rootItem) {
            DocumentObjectItem *childItem = *it->second->items.begin();
            if(!createNewItem(*childItem->object(),item,i,it->second))
                --i;
        }else {
            DocumentObjectItem *childItem = it->second->rootItem;
            if(item->isChildOfItem(childItem)) {
                Base::Console().Error("Gui::DocumentItem::populateItem(): Cyclic dependency in %s and %s\n",
                        item->object()->getObject()->Label.getValue(),
                        childItem->object()->getObject()->Label.getValue());
                --i;
                continue;
            }
            it->second->rootItem = 0;
            this->removeChild(childItem);
            item->insertChild(i,childItem);
        }
    }
    for(++i;item->childCount()>i;) {
        QTreeWidgetItem *ci = item->child(i);
        if (ci->type() == TreeWidget::ObjectType) {
            auto childItem = static_cast<DocumentObjectItem*>(ci);
            // Add the child item back to document root if it is the only
            // instance.  Now, because of the lazy loading strategy, this may
            // not truly be the last instance of the object. It may belong to
            // other parents not expanded yet. We don't want to traverse the
            // whole tree to confirm that. Just let it be. If the other
            // parent(s) later expanded, this child item will be moved from
            // root to its parent.
            if(childItem->requiredAtRoot()) {
                item->removeChild(childItem);
                this->addChild(childItem);
                childItem->myData->rootItem = childItem;
                continue;
            }
        }
        delete ci;
    }
    getTree()->updateGeometries();
}

void DocumentItem::checkRemoveChildrenFromRoot(const Gui::ViewProviderDocumentObject& view)
{
    auto it = ObjectMap.find(view.getObject());
    if(it != ObjectMap.end() && 
       it->second->removeChildrenFromRoot!=view.canRemoveChildrenFromRoot()) 
    {
        it->second->removeChildrenFromRoot = !it->second->removeChildrenFromRoot;
        for(auto item : it->second->items)
            populateItem(item,true);
    }
}

void DocumentItem::slotChangeObject(const Gui::ViewProviderDocumentObject& view, bool broadcast)
{
    if(broadcast) {
        for(auto &v : getTree()->DocumentMap)
            v.second->slotChangeObject(view,false);
        return;
    }

    auto it = ObjectMap.find(view.getObject());
    if(it == ObjectMap.end())
        return;

    bool changeLabel = false;
    const char *label = view.getObject()->Label.getValue();
    if(it->second->label != label) {
        changeLabel = true;
        it->second->label = label;
    }
    auto children = view.claimChildren();
    bool removeChildrenFromRoot = view.canRemoveChildrenFromRoot();
    bool doPopulate = false;
    if(removeChildrenFromRoot!=it->second->removeChildrenFromRoot || 
       children!=it->second->children) 
    {
        doPopulate = true;
        it->second->removeChildrenFromRoot = removeChildrenFromRoot;
        it->second->children.swap(children);
    }
    if(!changeLabel && !doPopulate) return;

    QString displayName;
    if(changeLabel)
        displayName = QString::fromUtf8(label);

    for(auto item : it->second->items) {
        if(changeLabel)
            item->setText(0, displayName);
        if(doPopulate)
            populateItem(item,true);
    }
}

void DocumentItem::slotRenameObject(const Gui::ViewProviderDocumentObject& obj)
{
    // Do nothing here because the Label is set in slotChangeObject
    Q_UNUSED(obj); 
}

void DocumentItem::slotActiveObject(const Gui::ViewProviderDocumentObject& obj)
{
    if(ObjectMap.find(obj.getObject()) == ObjectMap.end())
        return; // signal is emitted before the item gets created
    FOREACH_ITEM_ALL(item);
        QFont f = item->font(0);
        f.setBold(item->object() == &obj);
        item->setFont(0,f);
    END_FOREACH_ITEM
}

void DocumentItem::slotHighlightObject (const Gui::ViewProviderDocumentObject& obj,const Gui::HighlightMode& high,bool set)
{
    FOREACH_ITEM(item,obj)
        QFont f = item->font(0);
        switch (high) {
        case Gui::Bold: f.setBold(set);             break;
        case Gui::Italic: f.setItalic(set);         break;
        case Gui::Underlined: f.setUnderline(set);  break;
        case Gui::Overlined: f.setOverline(set);    break;
        case Gui::Blue:
            if(set)
                item->setBackgroundColor(0,QColor(200,200,255));
            else
                item->setData(0, Qt::BackgroundColorRole,QVariant());
            break;
        case Gui::LightBlue:
            if(set)
                item->setBackgroundColor(0,QColor(230,230,255));
            else
                item->setData(0, Qt::BackgroundColorRole,QVariant());
            break;
        default:
            break;
        }

        item->setFont(0,f);
    END_FOREACH_ITEM
}

void DocumentItem::slotExpandObject (const Gui::ViewProviderDocumentObject& obj,const Gui::TreeItemMode& mode)
{
    FOREACH_ITEM(item,obj)
        if (!item->parent() || // has no parent (see #0003025)
            !item->parent()->isExpanded()) continue;
        switch (mode) {
        case Gui::Expand:
            item->setExpanded(true);
            break;
        case Gui::Collapse:
            item->setExpanded(false);
            break;
        case Gui::Toggle:
            if (item->isExpanded())
                item->setExpanded(false);
            else
                item->setExpanded(true);
            break;

        default:
            // not defined enum
            assert(0);
        }
        populateItem(item);
    END_FOREACH_ITEM
}

const Gui::Document* DocumentItem::document() const
{
    return this->pDocument;
}

//void DocumentItem::markItem(const App::DocumentObject* Obj,bool mark)
//{
//    // never call without Object!
//    assert(Obj);
//
//
//    std::map<std::string,DocumentObjectItem*>::iterator pos;
//    pos = ObjectMap.find(Obj);
//    if (pos != ObjectMap.end()) {
//        QFont f = pos->second->font(0);
//        f.setUnderline(mark);
//        pos->second->setFont(0,f);
//    }
//}

//void DocumentItem::markItem(const App::DocumentObject* Obj,bool mark)
//{
//    // never call without Object!
//    assert(Obj);
//
//
//    std::map<std::string,DocumentObjectItem*>::iterator pos;
//    pos = ObjectMap.find(Obj);
//    if (pos != ObjectMap.end()) {
//        QFont f = pos->second->font(0);
//        f.setUnderline(mark);
//        pos->second->setFont(0,f);
//    }
//}

void DocumentItem::testStatus(void)
{
    for(const auto &v : ObjectMap)
        v.second->testStatus();
}

void DocumentItem::setData (int column, int role, const QVariant & value)
{
    if (role == Qt::EditRole) {
        QString label = value.toString();
        pDocument->getDocument()->Label.setValue((const char*)label.toUtf8());
    }

    QTreeWidgetItem::setData(column, role, value);
}

void DocumentItem::clearSelection(void)
{
    // Block signals here otherwise we get a recursion and quadratic runtime
    bool ok = treeWidget()->blockSignals(true);
    FOREACH_ITEM_ALL(item);
        item->selected = 0;
        item->setSelected(false);
    END_FOREACH_ITEM;
    treeWidget()->blockSignals(ok);
}

void DocumentItem::updateSelection(QTreeWidgetItem *ti, bool unselect) {
    for(int i=0,count=ti->childCount();i<count;++i) {
        auto child = ti->child(i);
        if(child && child->type()==TreeWidget::ObjectType) {
            auto childItem = static_cast<DocumentObjectItem*>(child);
            if(unselect) 
                childItem->setSelected(false);
            updateItemSelection(childItem);
            if(unselect && childItem->isGroup()) {
                // If the child item being force unselected by its group parent
                // is itself a group, propagate the unselection to its own
                // children
                updateSelection(childItem,true);
            }
        }
    }
        
    if(unselect) return;
    for(int i=0,count=ti->childCount();i<count;++i)
        updateSelection(ti->child(i));
}

void DocumentItem::updateItemSelection(DocumentObjectItem *item) {
    bool selected = item->isSelected();
    if((selected && item->selected) || (!selected && !item->selected)) 
        return;
    item->selected = selected;

    std::ostringstream str;
    auto obj = item->getSubName(str);
    if(!obj) return;
    const char *objname = obj->getNameInDocument();
    if(!objname) return;
    const char *docname = obj->getDocument()->getName();
    const auto &subname = str.str();

    if(subname.size()) {
        auto parentItem = item->getParentItem();
        assert(parentItem);
        if(selected && parentItem->selected) {
            FC_TRACE("force unselect parent");
            // When a group item is selected, all its children objects are
            // highlighted in the 3D view. So, when an item of some group is
            // newly selected, we must force unselect its parent in order to
            // show the selection highlight. Besides, select both the parent
            // group and its children doesn't make much sense.
            parentItem->setSelected(false);
            updateItemSelection(parentItem);
        }
    }

    if(selected && item->isGroup()) {
        // Same reasoning as above. When a group item is newly selected, We
        // choose to force unselect all its children to void messing up the
        // selection highlight 
        FC_TRACE("force unselect all children");
        updateSelection(item,true);
    }

    if(!selected)
        Gui::Selection().rmvSelection(docname,objname,subname.c_str());
    else if(!Gui::Selection().addSelection(docname,objname,subname.c_str())) {
        item->selected = 0;
        item->setSelected(false);
    }else 
        getTree()->syncView();
}

void DocumentItem::findSelection(bool sync, DocumentObjectItem *item, const char *subname) 
{
    if(item->isHidden())
        return;

    if(!subname || *subname==0) {
        item->selected=2;
        return;
    }

    FC_TRACE("find next " << subname);

    // try to find the next level object name
    const char *nextsub = 0;
    const char *dot = 0;
    if((dot=strchr(subname,'.'))) 
        nextsub = dot+1;
    else {
        item->selected=2;
        return;
    }

    if(!item->populated && sync) {
        //force populate the item
        item->populated = true;
        populateItem(item,true);
    }

    for(int i=0,count=item->childCount();i<count;++i) {
        auto ti = item->child(i);
        if(!ti || ti->type()!=TreeWidget::ObjectType) continue;
        auto child = static_cast<DocumentObjectItem*>(ti);
        const char *name = child->getName();
        if(!name) continue;

        // try to match the item name with the next object name (starting from
        // subname till dot)
        const char *s;
        for(s=subname;*name && s!=dot;++name,++s)
            if(*s!=*name) break;
        if(*name==0 && s==dot) {
            findSelection(sync,child,nextsub);
            return;
        }
    }

    // The sub object is not found. Maybe it is a non-object sub-element.
    // Select the current object instead.
    FC_TRACE("element " << subname << " not found");
    item->selected=2;
}

void DocumentItem::selectItems(bool sync) {
    const auto &sels = Selection().getSelection(pDocument->getDocument()->getName(),false);
    for(const auto &sel : sels) {
        auto it = ObjectMap.find(sel.pObject);
        if(it == ObjectMap.end()) continue;
        FC_TRACE("find select " << sel.FeatName);
        for(auto item : it->second->items) {
            // If the parent is a group, then we have full quanlified
            // selection, which means this item can never be selected directly
            // in the 3D view,  only as element of the parent object
            if(item->isParentGroup() == 1)
                continue;

            findSelection(sync,item,sel.SubName);
        }
    }

    DocumentObjectItem *first = 0;

    FOREACH_ITEM_ALL(item)
        if(item->selected == 1) {
            // this means it is the old selection and is not in the current
            // selection
            item->selected = 0;
            item->setSelected(false);
        }else if(item->selected) {
            item->selected = 1;
            item->setSelected(true);
            if(first) 
                sync = false;
            else
                first = item;
        }
    END_FOREACH_ITEM;

    if(sync && first) 
        treeWidget()->scrollToItem(first);
}

void DocumentItem::selectLinkedItem(DocumentObjectItem *item, bool recurse) { 
    ViewProviderDocumentObject *linked = item->object()->getLinkedView(recurse);
    if(!linked || linked == item->object()) return;

    DocumentItem *linkedDoc = this;
    if(linked->getDocument()!=pDocument) {
        linkedDoc = getTree()->getDocumentItem(linked->getDocument());
        if(!linkedDoc) return;
    }

    auto it = linkedDoc->ObjectMap.find(linked->getObject());
    if(it == linkedDoc->ObjectMap.end()) return;
    auto linkedItem = it->second->rootItem;
    if(!linkedItem) 
        linkedItem = *it->second->items.begin();

    if(showItem(linkedItem,true)) {
        item->setSelected(false);
        treeWidget()->scrollToItem(linkedItem);
    }

    if(linkedDoc != this) {
        MDIView *view = linkedDoc->pDocument->getActiveView();
        if (view) getMainWindow()->setActiveWindow(view);
    }
}

void DocumentItem::populateParents(const ViewProvider *vp, ParentMap &parentMap) {
    auto it = parentMap.find(vp);
    if(it == parentMap.end()) return;
    for(auto parent : it->second) {
        auto it = ObjectMap.find(parent->getObject());
        if(it==ObjectMap.end())
            continue;

        populateParents(parent,parentMap);
        for(auto item : it->second->items) {
            if(!item->isHidden() && !item->populated) {
                item->populated = true;
                populateItem(item,true);
            }
        }
    }
}

void DocumentItem::selectAllInstances(const ViewProviderDocumentObject &vpd) {
    ParentMap parentMap;
    auto pObject = vpd.getObject();
    if(ObjectMap.find(pObject) == ObjectMap.end())
        return;

    bool lock = getTree()->blockConnection(true);

    // We are trying to select all items corresponding to a given view
    // provider, i.e. all apperance of the object inside all its parent items
    //
    // Build a map of object to all its parent    
    for(auto &v : ObjectMap) {
        if(v.second->viewObject == &vpd) continue;
        for(auto child : v.second->viewObject->claimChildren()) {
            auto vp = getViewProvider(child);
            if(!vp) continue;
            parentMap[vp].push_back(v.second->viewObject);
        }
    }

    // now make sure all parent items are populated. In order to do that, we
    // need to populate the oldest parent first
    populateParents(&vpd,parentMap);

    DocumentObjectItem *first = 0;
    FOREACH_ITEM(item,vpd);
        if(showItem(item,true) && !first)
            first = item;
    END_FOREACH_ITEM;

    getTree()->blockConnection(lock);
    if(first) {
        treeWidget()->scrollToItem(first);
        updateSelection();
    }
}

bool DocumentItem::showHidden() const {
    return pDocument->getDocument()->ShowHidden.getValue();
}

void DocumentItem::setShowHidden(bool show) {
    pDocument->getDocument()->ShowHidden.setValue(show);
}

bool DocumentItem::showItem(DocumentObjectItem *item, bool select) {
    auto parent = item->parent();
    if(item->isHidden() ||
       (parent->type()==TreeWidget::ObjectType && 
        !showItem(static_cast<DocumentObjectItem*>(parent),false)))
        return false;

    parent->setExpanded(true);
    if(select) item->setSelected(true);
    return true;
}

void DocumentItem::setItemVisibility(const ViewProviderDocumentObject &vpd) {
    bool show = showHidden();
    FOREACH_ITEM(item,vpd);
        item->setHidden(!vpd.showInTree() && !show);
        item->testStatus(false);
    END_FOREACH_ITEM;
}

void DocumentItem::updateItemsVisibility(QTreeWidgetItem *item, bool show) {
    for(int i=0;i<item->childCount();++i) {
        auto child = item->child(i);
        if(child->type()!=TreeWidget::ObjectType) continue;
        auto childItem = static_cast<DocumentObjectItem*>(child);
        childItem->setHidden(!show && !childItem->object()->showInTree());
        updateItemsVisibility(childItem,show);
    }
}

void DocumentItem::updateSelection() {
    bool lock = getTree()->blockConnection(true);
    updateSelection(this,false);
    getTree()->blockConnection(lock);
}

// ----------------------------------------------------------------------------

DocumentObjectItem::DocumentObjectItem(DocumentObjectDataPtr data)
    : QTreeWidgetItem(TreeWidget::ObjectType)
    , myData(data), previousStatus(-1),selected(0),populated(false)
{
    setFlags(flags()|Qt::ItemIsEditable);
    myData->items.insert(this);
}

DocumentObjectItem::~DocumentObjectItem()
{
    auto it = myData->items.find(this);
    if(it == myData->items.end())
        assert(0);
    else
        myData->items.erase(it);

    if(myData->rootItem == this)
        myData->rootItem = 0;
}

Gui::ViewProviderDocumentObject* DocumentObjectItem::object() const
{
    return myData->viewObject;
}

void DocumentObjectItem::testStatus(bool resetStatus) {
    QIcon icon,icon2;
    testStatus(resetStatus,icon,icon2);
}

void DocumentObjectItem::testStatus(bool resetStatus,QIcon &icon1, QIcon &icon2)
{
    App::DocumentObject* pObject = object()->getObject();

    int visible = -1;
    auto parentItem = getParentItem();
    if(parentItem && parentItem->object()->hasChildElement())
        visible = parentItem->object()->isElementVisible(pObject->getNameInDocument());
    if(visible<0)
        visible = object()->isShow()?1:0;

    bool external = object()->getDocument()!=myData->docItem.document() ||
        object()->getLinkedView(false)->getDocument()!=object()->getDocument();

    int currentStatus =
        ((external?0:1)<<4) |
        ((object()->showInTree() ? 0 : 1) << 3) |
        ((pObject->isError()          ? 1 : 0) << 2) |
        ((pObject->mustExecute() == 1 ? 1 : 0) << 1) |
        (visible         ? 1 : 0);

    if (!resetStatus && previousStatus==currentStatus)
        return;

    previousStatus = currentStatus;

    QIcon::Mode mode = QIcon::Normal;
    if (currentStatus & 1) { // visible
        // Note: By default the foreground, i.e. text color is invalid
        // to make use of the default color of the tree widget's palette.
        // If we temporarily set this color to dark and reset to an invalid
        // color again we cannot do it with setTextColor() or setForeground(),
        // respectively, because for any reason the color would always switch
        // to black which will lead to unreadable text if the system background
        // hss already a dark color.
        // However, it works if we set the appropriate role to an empty QVariant().
#if QT_VERSION >= 0x040200
        this->setData(0, Qt::ForegroundRole,QVariant());
#else
        this->setData(0, Qt::TextColorRole,QVariant());
#endif
    }
    else { // invisible
        QStyleOptionViewItem opt;
        // it can happen that a tree item is not attached to the tree widget (#0003025)
        if (this->treeWidget())
            opt.initFrom(this->treeWidget());
#if QT_VERSION >= 0x040200
        this->setForeground(0, opt.palette.color(QPalette::Disabled,QPalette::Text));
#else
        this->setTextColor(0, opt.palette.color(QPalette::Disabled,QPalette::Text);
#endif
        mode = QIcon::Disabled;
    }

    QIcon &icon = mode==QIcon::Normal?icon1:icon2;

    if(icon.isNull()) {
        QPixmap px;
        if (currentStatus & 4) {
            static QPixmap pxError;
            if(pxError.isNull()) {
            // object is in error state
                const char * const feature_error_xpm[]={
                    "9 9 3 1",
                    ". c None",
                    "# c #ff0000",
                    "a c #ffffff",
                    "...###...",
                    ".##aaa##.",
                    ".##aaa##.",
                    "###aaa###",
                    "###aaa###",
                    "#########",
                    ".##aaa##.",
                    ".##aaa##.",
                    "...###..."};
                pxError = QPixmap(feature_error_xpm);
            }
            px = pxError;
        }
        else if (currentStatus & 2) {
            static QPixmap pxRecompute;
            if(pxRecompute.isNull()) {
                // object must be recomputed
                const char * const feature_recompute_xpm[]={
                    "9 9 3 1",
                    ". c None",
                    "# c #0000ff",
                    "a c #ffffff",
                    "...###...",
                    ".######aa",
                    ".#####aa.",
                    "#####aa##",
                    "#aa#aa###",
                    "#aaaa####",
                    ".#aa####.",
                    ".#######.",
                    "...###..."};
                pxRecompute = QPixmap(feature_recompute_xpm);
            }
            px = pxRecompute;
        }

        // get the original icon set
        QIcon icon_org = object()->getIcon();

        // Icon size from PM_ListViewIconSize is too big, and the TreeView will
        // automatically scale down the icon to fit, which in turn causes the
        // overlay status icon being scaled down too much. Use Qt standard icon
        // size instead
        //
        // int w = QApplication::style()->pixelMetric(QStyle::PM_ListViewIconSize);
        static int w = -1;
        if(w < 0) w = QApplication::style()->standardPixmap(QStyle::SP_DirClosedIcon).width();

        QPixmap pxOn,pxOff;

        // if needed show small pixmap inside
        if (!px.isNull()) {
            pxOff = BitmapFactory().merge(icon_org.pixmap(w, w, mode, QIcon::Off),
                px,BitmapFactoryInst::TopRight);
            pxOn = BitmapFactory().merge(icon_org.pixmap(w, w, mode, QIcon::On ),
                px,BitmapFactoryInst::TopRight);
        } else {
            pxOff = icon_org.pixmap(w, w, mode, QIcon::Off);
            pxOn = icon_org.pixmap(w, w, mode, QIcon::On);
        }

        if(currentStatus & 8)  {// hidden item
            static QPixmap pxHidden;
            if(pxHidden.isNull()) {
                const char * const feature_hidden_xpm[]={
                    "9 7 3 1",
                    ". c None",
                    "# c #000000",
                    "a c #ffffff",
                    "...###...",
                    "..#aaa#..",
                    ".#a###a#.",
                    "#aa###aa#",
                    ".#a###a#.",
                    "..#aaa#..",
                    "...###..."};
                pxHidden = QPixmap(feature_hidden_xpm);
            }
            pxOff = BitmapFactory().merge(pxOff, pxHidden, BitmapFactoryInst::TopLeft);
            pxOn = BitmapFactory().merge(pxOn, pxHidden, BitmapFactoryInst::TopLeft);
        }

        if(external) {// external item
            static QPixmap pxExternal;
            if(pxExternal.isNull()) {
                const char * const feature_external_xpm[]={
                    "7 7 3 1",
                    ". c None",
                    "# c #000000",
                    "a c #ffffff",
                    "..###..",
                    ".#aa##.",
                    "..#aa##",
                    "..##aa#",
                    "..#aa##",
                    ".#aa##.",
                    "..###.."};
                pxExternal = QPixmap(feature_external_xpm);
            }
            pxOff = BitmapFactory().merge(pxOff, pxExternal, BitmapFactoryInst::BottomRight);
            pxOn = BitmapFactory().merge(pxOn, pxExternal, BitmapFactoryInst::BottomRight);
        }

        icon.addPixmap(pxOn, QIcon::Normal, QIcon::On);
        icon.addPixmap(pxOff, QIcon::Normal, QIcon::Off);
    }

    this->setIcon(0, icon);
}

void DocumentObjectItem::displayStatusInfo()
{
    App::DocumentObject* Obj = object()->getObject();

    QString info = QString::fromLatin1(Obj->getStatusString());
    if ( Obj->mustExecute() == 1 )
        info += QString::fromLatin1(" (but must be executed)");
    QString status = TreeWidget::tr("%1, Internal name: %2")
            .arg(info)
            .arg(QString::fromLatin1(Obj->getNameInDocument()));
    getMainWindow()->showMessage(status);

    if (Obj->isError()) {
        QTreeWidget* tree = this->treeWidget();
        QPoint pos = tree->visualItemRect(this).topRight();
        QToolTip::showText(tree->mapToGlobal(pos), info);
    }
}

void DocumentObjectItem::setExpandedStatus(bool on)
{
    App::DocumentObject* Obj = object()->getObject();
    Obj->setStatus(App::Expand, on);
}

void DocumentObjectItem::setData (int column, int role, const QVariant & value)
{
    QTreeWidgetItem::setData(column, role, value);
    if (role == Qt::EditRole) {
        QString label = value.toString();
        object()->getObject()->Label.setValue((const char*)label.toUtf8());
    }
}

bool DocumentObjectItem::isChildOfItem(DocumentObjectItem* item)
{
    for(auto pitem=parent();pitem;pitem=pitem->parent())
        if(pitem == item)
            return true;
    return false;
}

bool DocumentObjectItem::requiredAtRoot(bool excludeSelf) const{
    if(myData->rootItem || object()->getDocument()!=myData->docItem.document()) 
        return false;
    for(auto item : myData->items) {
        if(excludeSelf && item == this) continue;
        auto pi = item->getParentItem();
        if(!pi || pi->myData->removeChildrenFromRoot)
            return false;
    }
    return true;
}

bool DocumentObjectItem::isLink() const {
    return object()->getLinkedView(false) != object();
}

bool DocumentObjectItem::isLinkFinal() const {
    return object()->getLinkedView(false) == object()->getLinkedView(true);
}


bool DocumentObjectItem::isParentLink() const {
    auto pi = getParentItem();
    return pi && pi->isLink();
}

int DocumentObjectItem::isGroup() const {
    if(object()->hasChildElement())
        return 1;
    if(object()->getLinkedView(true)->getChildRoot())
        return 2;
    return 0;
}

int DocumentObjectItem::isParentGroup() const {
    auto pi = getParentItem();
    return pi?pi->isGroup():0;
}

DocumentObjectItem *DocumentObjectItem::getParentItem() const{
    if(parent()->type()!=TreeWidget::ObjectType)
        return 0;
    return static_cast<DocumentObjectItem*>(parent());
}

const char *DocumentObjectItem::getName() const {
    const char *name = object()->getObject()->getNameInDocument();
    return name?name:"";
}

App::DocumentObject *DocumentObjectItem::getSubName(std::ostringstream &str) const 
{
    const char *name = object()->getObject()->getNameInDocument();
    if(!name) return 0;
    auto parent = getParentItem();

    int group;
    if(!parent || !(group=parent->isGroup())) 
        return object()->getObject();

    auto obj = parent->getSubName(str);
    if(!obj) return 0;

    if(str.tellp()>0 || group==1)
        str << name << '.';
    else 
        obj = object()->getObject();
    return obj;
}

App::DocumentObject *DocumentObjectItem::getFullSubName(std::ostringstream &str) const {
    if(!isParentGroup())
        return object()->getObject();

    auto ret = getParentItem()->getFullSubName(str);
    str << getName() << '.';
    return ret;
}


#include "moc_Tree.cpp"

