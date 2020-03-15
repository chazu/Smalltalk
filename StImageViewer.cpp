/*
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the Smalltalk parser/compiler library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
*/

#include "StImageViewer.h"
#include "StObjectMemory.h"
#include <QAbstractItemModel>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QTreeView>
#include <QTreeWidget>
#include <QtDebug>
#include <QDesktopWidget>
#include <QCloseEvent>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QLabel>
#include <QShortcut>
using namespace St;

class ImageViewer::Model : public QAbstractItemModel
{
public:
    explicit Model(QTreeView *parent = 0):QAbstractItemModel(parent)
    {
        d_knowns.insert(65535,"objectMinusOne");
        d_knowns.insert(1,"objectZero");
        d_knowns.insert(3,"objectOne");
        d_knowns.insert(5,"objectTwo");
        d_knowns.insert(0x02,"nil");
        d_knowns.insert(0x04,"false");
        d_knowns.insert(0x06,"true");
        d_knowns.insert(0x08,"processor");
        d_knowns.insert(0x12,"smalltalk");

        d_knowns.insert(0x0c,"SmallInteger");
        d_knowns.insert(0x0e,"String");
        d_knowns.insert(0x10,"Array");
        d_knowns.insert(0x14,"Float");
        d_knowns.insert(0x16,"MethodContext");
        d_knowns.insert(0x18,"BlockContext");
        d_knowns.insert(0x1a,"Point");
        d_knowns.insert(0x1c,"LargePositiveInteger");
        d_knowns.insert(0x1e,"DisplayBitmap");
        d_knowns.insert(0x20,"Message");
        d_knowns.insert(0x22,"CompiledMethod");
        d_knowns.insert(0x26,"Semaphore");
        d_knowns.insert(0x28,"Character");

        d_knowns.insert(0x0a,"symbolTable");
        d_knowns.insert(0x2a,"symbolDoesNotUnderstand");
        d_knowns.insert(0x2c,"symbolCannotReturn");
        d_knowns.insert(0x2e,"symbolMonitor");
        d_knowns.insert(0x24,"symbolUnusedOop18");
        d_knowns.insert(0x34,"symbolMustBeBoolean");
        d_knowns.insert(0x30,"specialSelectors");
        d_knowns.insert(0x32,"characterTable");

        // empirical additions, not in the Blue Book
        d_knowns.insert(ObjectMemory::classSymbol, "Symbol");
        d_knowns.insert(0x84, "Association");
    }

    QTreeView* getParent() const
    {
        return static_cast<QTreeView*>(QObject::parent());
    }

    void setOm( ObjectMemory* om, quint16 root = 0 )
    {
        beginResetModel();
        d_root = Slot();
        d_om = om;
        if( root )
        {
            const quint16 size = d_om->fetchWordLenghtOf( root );
            d_root.d_oop = root;
            d_root.d_kind = Slot::Frame;
            if( size )
                fill( &d_root, d_om->fetchClassOf(root), size, true );
        }else
            fillTop();
        endResetModel();
    }

    ObjectMemory* getOm() const { return d_om; }

    quint16 getValue(const QModelIndex& index) const
    {
        if( !index.isValid() || d_om == 0 )
            return 0;
        Slot* s = static_cast<Slot*>( index.internalPointer() );
        Q_ASSERT( s != 0 );
        return s->d_oop;
    }

    QModelIndex findValue( quint16 oop )
    {
        for( int i = 0; i < d_root.d_children.size(); i++ )
        {
            if( d_root.d_children[i]->d_oop == oop )
                return createIndex( i, 0, d_root.d_children[i] );
        }
        return QModelIndex();
    }

    // overrides
    int columnCount ( const QModelIndex & parent = QModelIndex() ) const { return 3; }

    QVariant headerData(int section, Qt::Orientation orientation, int role ) const
    {
        if( orientation == Qt::Horizontal && role ==  Qt::DisplayRole )
        {
            switch(section)
            {
            case 0:
                return "oop";
            case 1:
                return "class";
            case 2:
                return "value";
            }
        }
        return QVariant();
    }

    QVariant data ( const QModelIndex & index, int role = Qt::DisplayRole ) const
    {
        if( !index.isValid() || d_om == 0 )
            return QVariant();

        Slot* s = static_cast<Slot*>( index.internalPointer() );
        Q_ASSERT( s != 0 );
        switch( role )
        {
        case Qt::DisplayRole:
            switch( index.column() )
            {
            case 0:
                if( s->d_kind == Slot::Bytecode )
                    return "bytecode";
                else
                {
                    if( s->d_kind == Slot::Continuation )
                        return "...";
                    QByteArray str = d_knowns.value(s->d_oop);
                    if( !str.isEmpty() )
                        return str;
                    else
                        return QByteArray::number( s->d_oop, 16 );
                }
                break;
            case 1:
                if( s->d_kind != Slot::Continuation && s->d_kind != Slot::Bytecode )
                {
                    const quint16 cls = d_om->fetchClassOf(s->d_oop);
                    QByteArray str = d_knowns.value(cls);
                    if( !str.isEmpty() )
                        return str;
                    else
                    {
                        return d_om->fetchClassName(cls);
#if 0
                        const quint16 nameId = d_om->fetchWordOfObject(6,cls);
                        const quint16 nameCls = d_om->fetchClassOf(nameId);
                        if( nameCls == 0x38 )
                        {
                            str = (const char*)d_om->fetchByteString(nameId).d_bytes;
                            if( !str.isEmpty() )
                                return str;
                        }else if( nameCls == cls )
                        {
                            const quint16 nameId2 = d_om->fetchWordOfObject(6,s->d_oop);
                            const quint16 nameCls2 = d_om->fetchClassOf(nameId2);
                            if( nameCls2 == 0x38 )
                            {
                                str = (const char*)d_om->fetchByteString(nameId2).d_bytes;
                                if( !str.isEmpty() )
                                    return str + " class";
                            }
                        }
                        return QByteArray::number( cls, 16 );
#endif
                    }
                }
                break;
            case 2:
                {
                    switch( s->d_kind )
                    {
                    case Slot::String:
                        return "\"" + QByteArray((const char*)d_om->fetchByteString(s->d_oop).d_bytes) + "\"";
                    case Slot::Character:
                        {
                            quint16 ch = d_om->fetchWordOfObject(0,s->d_oop);
                            ch = ch >> 1;
                            if( ::isprint(ch) )
                                return "'" + QByteArray(1,ch) + "'";
                            else
                                return "0x" + QByteArray::number(ch,16);
                        }
                        break;
                    case Slot::Chunk:
                        {
                            QByteArray str((const char*)d_om->fetchByteString(s->d_oop).d_bytes);
                            const int len = 16;
                            if( str.size() > len )
                                return str.left(len).toHex() + "... (" + QByteArray::number(str.size()) + " bytes)";
                            else
                                return str.toHex() + " (" + QByteArray::number(str.size()) + " bytes)";
                        }
                        break;
                    case Slot::Bytecode:
                        {
                            ObjectMemory::ByteString bs = d_om->methodBytecodes(s->d_oop);
                            QByteArray str((const char*)bs.d_bytes, bs.d_len );
                            const int len = 16;
                            if( bs.d_len > len )
                                return str.left(len).toHex() + "... (" + QByteArray::number(bs.d_len) + " bytes)";
                            else
                                return str.toHex() + " (" + QByteArray::number(bs.d_len) + " bytes)";
                        }
                        break;
                    case Slot::Int:
                        return QString::number(ObjectMemory::toInt(s->d_oop));
                    case Slot::Continuation:
                        return QString("another %1 entries").arg(s->d_oop);
                    }
                }
            }
            break;
        case Qt::ToolTipRole:
            if( s->d_kind != Slot::Continuation && s->d_kind != Slot::Bytecode )
            {
                const quint16 cls = d_om->fetchClassOf(s->d_oop);
                switch( index.column() )
                {
                case 0:
                    return QString("object oop %1").arg(s->d_oop,0,16);
                case 1:
                    return QString("class oop %1").arg(cls,0,16);
                }
            }
            break;
        default:
            break;
        }
        return QVariant();
    }

    QModelIndex parent ( const QModelIndex & index ) const
    {
        if( index.isValid() )
        {
            Slot* s = static_cast<Slot*>( index.internalPointer() );
            Q_ASSERT( s != 0 );
            if( s->d_parent == &d_root )
                return QModelIndex();
            // else
            Q_ASSERT( s->d_parent != 0 );
            Q_ASSERT( s->d_parent->d_parent != 0 );
            return createIndex( s->d_parent->d_parent->d_children.indexOf( s->d_parent ), 0, s->d_parent );
        }else
            return QModelIndex();
    }

    int rowCount ( const QModelIndex & parent ) const
    {
        if( parent.isValid() )
        {
            Slot* s = static_cast<Slot*>( parent.internalPointer() );
            Q_ASSERT( s != 0 );
            return s->d_children.size();
        }else
            return d_root.d_children.size();
    }

    QModelIndex index ( int row, int column, const QModelIndex & parent ) const
    {
        const Slot* s = &d_root;
        if( parent.isValid() )
        {
            s = static_cast<Slot*>( parent.internalPointer() );
            Q_ASSERT( s != 0 );
        }
        if( row < s->d_children.size() && column < columnCount( parent ) )
            return createIndex( row, column, s->d_children[row] );
        else
            return QModelIndex();
    }

    bool hasChildren( const QModelIndex & parent ) const
    {
        if( d_om == 0 )
            return false;
        if( !parent.isValid() )
            return true;

        Slot* s = static_cast<Slot*>( parent.internalPointer() );
        Q_ASSERT( s != 0 );
        if( !s->d_children.isEmpty() )
            return true;

        switch( s->d_kind )
        {
        case Slot::String:
        case Slot::Character:
        case Slot::Chunk:
        case Slot::Int:
        case Slot::Continuation:
        case Slot::Bytecode:
            return false;
        }

        const quint16 size = d_om->fetchWordLenghtOf( s->d_oop );
        if( size == 0 )
            return false;

        const quint16 cls = d_om->fetchClassOf(s->d_oop);

        const_cast<Model*>(this)->fill(s, cls, size);
        return true;
    }

    Qt::ItemFlags flags( const QModelIndex & index ) const
    {
        Q_UNUSED(index)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable; //  | Qt::ItemIsDragEnabled;
    }


    struct Slot
    {
        enum Kind { Frame, Int, String, Character, Chunk, Method, Continuation, Bytecode };
        quint16 d_oop;
        quint8 d_kind;
        QList<Slot*> d_children;
        Slot* d_parent;
        Slot(Slot* p = 0):d_parent(p),d_kind(Frame){ if( p ) p->d_children.append(this); }
        ~Slot() { foreach( Slot* s, d_children ) delete s; }
    };

    void setKind( Slot* s, quint16 cls )
    {
        switch( cls )
        {
        case ObjectMemory::classString:
        case 0x38: // Symbol
            s->d_kind = Slot::String;
            break;
        case ObjectMemory::classCharacter: // Char
            s->d_kind = Slot::Character;
            break;
        case ObjectMemory::classCompiledMethod:
            s->d_kind = Slot::Method;
            break;
        case ObjectMemory::classSmallInteger:
            s->d_kind = Slot::Int;
            break;
        default:
            if( !d_om->hasPointerMembers(s->d_oop) )
                s->d_kind = Slot::Chunk;
            else
            {
                int size = d_om->fetchWordLenghtOf(s->d_oop);
                if( size > 1000 )
                    qWarning() << "pointer object oop" << QByteArray::number(s->d_oop,16) << "with"
                               << size << "slots";
            }
            break;
        }
    }

    void fill(Slot* super, quint16 cls, quint16 size, bool all = false )
    {
        if( cls == ObjectMemory::classCompiledMethod )
        {
            for( int i = 0; i < d_om->methodLiteralCount(super->d_oop); i++ )
            {
                Slot* s = new Slot();
                s->d_parent = super;
                s->d_oop = d_om->methodLiteral(i, super->d_oop);
                setKind(s,d_om->fetchClassOf(s->d_oop));
                super->d_children.append( s );
            }
            Slot* s = new Slot();
            s->d_parent = super;
            s->d_oop = super->d_oop;
            s->d_kind = Slot::Bytecode;
            super->d_children.append( s );
        }else
        {
            const quint16 max = all ? size : 50;
            Q_ASSERT( super->d_kind == Slot::Frame );
            for( int i = 0; i < qMin(size,max); i++ )
            {
                Slot* s = new Slot();
                s->d_parent = super;
                s->d_oop = d_om->fetchWordOfObject(i,super->d_oop);
                setKind(s,d_om->fetchClassOf(s->d_oop));
                super->d_children.append( s );
            }
            if( size > max )
            {
                Slot* s = new Slot();
                s->d_parent = super;
                s->d_oop = size - max;
                s->d_kind = Slot::Continuation;
                super->d_children.append( s );
            }
        }
    }

    void fillTop()
    {
        if( d_om == 0 )
            return;
        QList<quint16> oops = d_om->getAllValidOop();
        foreach( quint16 oop, oops )
        {
            quint16 cls = d_om->fetchClassOf(oop);
            if( cls == 0x38 || cls == 0x28 || cls == 0x0e || cls == 0x14 || cls == 0xcb0
                    || cls == 0x1a || cls == 0x1c )
                continue; // no toplevel Symbol, String or Char, or Float, or Point, or Rectangle
            // or LargePositiveInteger
            if( cls == 0x84 && !d_knowns.contains(oop) )
                continue; // only known assocs
            Slot* s = new Slot();
            s->d_parent = &d_root;
            s->d_oop = oop;
            setKind(s, cls);
            d_root.d_children.append( s );

            //const quint16 size = d_om->fetchWordLenghtOf( s->d_oop );
            //fill( s, cls, size );
        }
    }

    Slot d_root;
    ObjectMemory* d_om;
    QHash<quint16,QByteArray> d_knowns;
};

ImageViewer::ImageViewer():d_pushBackLock(false)
{
    d_om = new ObjectMemory(this);

    setDockNestingEnabled(true);
    setCorner( Qt::BottomRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::BottomLeftCorner, Qt::LeftDockWidgetArea );
    setCorner( Qt::TopRightCorner, Qt::RightDockWidgetArea );
    setCorner( Qt::TopLeftCorner, Qt::LeftDockWidgetArea );

    createObjectTable();
    createClasses();
    createDetail();
    createXref();

    QSettings s;

    const QRect screen = QApplication::desktop()->screenGeometry();
    resize( screen.width() - 20, screen.height() - 30 ); // so that restoreState works
    if( s.value("Fullscreen").toBool() )
        showFullScreen();
    else
        showMaximized();

    const QVariant state = s.value( "DockState" );
    if( !state.isNull() )
        restoreState( state.toByteArray() );

    setWindowTitle(tr("%1 %2").arg(qApp->applicationName()).arg(qApp->applicationVersion()));

    new QShortcut(tr("ALT+LEFT"),this,SLOT(onGoBack()));
    new QShortcut(tr("ALT+RIGHT"),this,SLOT(onGoForward()));
}

bool ImageViewer::parse(const QString& path)
{
    QFile in(path);
    if( !in.open(QIODevice::ReadOnly) )
        return false;
    const bool res = d_om->readFrom(&in);
    if( !res )
        QMessageBox::critical(this,tr("Loading Smalltalk-80 Image"), tr("Incompatible format.") );
    else
        d_mdl->setOm(d_om);
    d_backHisto.clear();
    d_forwardHisto.clear();
    fillClasses();
    return res;
}

void ImageViewer::createObjectTable()
{
    QDockWidget* dock = new QDockWidget( tr("Object Table"), this );
    dock->setObjectName("ObjectTable");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable );
    d_tree = new ObjectTree(this);
    d_mdl = new Model(d_tree);
    d_tree->setModel(d_mdl);
    dock->setWidget(d_tree);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect( d_tree, SIGNAL(sigObject(quint16)), this, SLOT(onObject(quint16)) );
}

void ImageViewer::createClasses()
{
    QDockWidget* dock = new QDockWidget( tr("Classes"), this );
    dock->setObjectName("Classes");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_classes = new QTreeWidget(dock);
    d_classes->setHeaderHidden(true);
    d_classes->setAlternatingRowColors(true);
    d_classes->setRootIsDecorated(false);
    dock->setWidget(d_classes);
    addDockWidget( Qt::LeftDockWidgetArea, dock );
    connect( d_classes, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onClassesClicked()) );
}

void ImageViewer::fillClasses()
{
    d_classes->clear();
    foreach( const quint16 cls, d_om->getClasses() )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_classes);
        item->setText( 0, d_om->fetchClassName(cls) );
        item->setData( 0, Qt::UserRole, cls );
    }
    foreach( const quint16 meta, d_om->getMetaClasses() )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(d_classes);
        item->setText( 0, d_om->fetchClassName(meta) );
        item->setData( 0, Qt::UserRole, meta );
    }
    d_classes->sortByColumn(0, Qt::AscendingOrder);
}

void ImageViewer::createXref()
{
    QDockWidget* dock = new QDockWidget( tr("Xref"), this );
    dock->setObjectName("Xref");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    QWidget* pane = new QWidget(dock);
    QVBoxLayout* vbox = new QVBoxLayout(pane);
    vbox->setMargin(0);
    vbox->setSpacing(0);
    d_xrefTitle = new QLabel(pane);
    d_xrefTitle->setMargin(2);
    d_xrefTitle->setWordWrap(true);
    vbox->addWidget(d_xrefTitle);
    d_xref = new QTreeWidget(pane);
    d_xref->setAlternatingRowColors(true);
    d_xref->setHeaderLabels(QStringList() << "oop" << "class");
    d_xref->setRootIsDecorated(false);
    vbox->addWidget(d_xref);
    dock->setWidget(pane);
    addDockWidget( Qt::RightDockWidgetArea, dock );
    connect( d_xrefTitle, SIGNAL(linkActivated(QString)), this, SLOT(onLink(QString)) );
    connect( d_xref, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(onXrefClicked(QTreeWidgetItem*,int)) );
}

void ImageViewer::fillXref(quint16 oop)
{
    d_xref->clear();
    const quint16 cls = d_om->fetchClassOf(oop);
    const QByteArray name = d_om->fetchClassName(cls);
    d_xrefTitle->setText( QString("oop %1 of <a href=\"oop:%2\">%3</a> is referenced by:").arg( oop, 0, 16 ).
                          arg( cls, 0, 16).arg(name.constData()) );
    QList<quint16> refs = d_om->getXref().value(oop);
    std::sort( refs.begin(), refs.end() );
    for( int i = 0; i < refs.size(); i++ )
    {
        QTreeWidgetItem* item = new QTreeWidgetItem( d_xref);
        item->setText(0,QString::number(refs[i],16) );
        item->setData(0,Qt::UserRole,refs[i]);
        const quint16 cls = d_om->fetchClassOf(refs[i]);
        item->setText( 1, d_om->fetchClassName(cls) );
        item->setData(1, Qt::UserRole, cls );
    }
}

void ImageViewer::createDetail()
{
    QDockWidget* dock = new QDockWidget( tr("Detail"), this );
    dock->setObjectName("Detail");
    dock->setAllowedAreas( Qt::AllDockWidgetAreas );
    dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
    d_detail = new QTextBrowser(dock);
    d_detail->setOpenLinks(false);
    dock->setWidget(d_detail);
    addDockWidget( Qt::RightDockWidgetArea, dock );
    connect( d_detail, SIGNAL(anchorClicked(QUrl)), this, SLOT(onLink(QUrl)) );
}

void ImageViewer::closeEvent(QCloseEvent* event)
{
    QSettings s;
    s.setValue( "DockState", saveState() );
    event->setAccepted(true);
}

void ImageViewer::showDetail(quint16 oop)
{
    d_detail->setHtml( detailText(oop) );
}

QString ImageViewer::detailText(quint16 oop)
{
    if( !ObjectMemory::isPointer(oop) )
        return QString("SmallInteger %1").arg( ObjectMemory::toInt(oop) );
    else if( d_om->getObjects().contains(oop) )
        return objectDetailText(oop);
    else if( oop )
        return classDetailText(oop);
    else
        return QString();
}

QString ImageViewer::objectDetailText(quint16 oop)
{
    const quint16 cls = d_om->fetchClassOf(oop);

    if( cls == ObjectMemory::classCompiledMethod )
        return methodDetailText(oop);

    QString html;
    QTextStream out(&html);

    out << "<html>";
    out << "<h2>Instance " << QString::number(oop,16) << " of <a href=\"oop:" << QString::number(cls,16) << "\">"
        << d_om->fetchClassName(cls) << "</a></h2>";

    const QByteArrayList fields = fieldList(cls);

    if( !fields.isEmpty() )
    {
        out << "<br><table width=100% align=left border=1 cellspacing=0 cellpadding=3>";
        out << "<tr><th>Field</th><th>Value</th></tr>";
        for( int i = 0; i < fields.size(); i++ )
        {
            out << "<tr><td>" << i << " " << fields[i] << "</td> <td>";
            quint16 val = d_om->fetchWordOfObject(i, oop);
            out << prettyValue(val);
            out << "</td></tr>";
        }
        out << "</table>";
    }
    const quint16 len = d_om->fetchWordLenghtOf(oop);
    if( len > fields.size() )
    {
        if( d_om->hasPointerMembers(oop) )
        {
            out << "<br><table width=100% align=left border=1 cellspacing=0 cellpadding=3>";
            out << "<tr><th>Index</th><th>Value</th></tr>";
            for( int i = fields.size(); i < len; i++ )
            {
                out << "<tr><td>" << i << "</td> <td>";
                quint16 val = d_om->fetchWordOfObject(i, oop);
                out << prettyValue(val);
                out << "</td></tr>";
            }
            out << "</table>";
        }else
            out << prettyValue(oop);
    }


    out << "</html>";

    return html;
}

static bool lessThan( const QPair< QString, quint16 >& lhs, const QPair< QString, quint16 >& rhs )
{
    return lhs.first.compare( rhs.first, Qt::CaseInsensitive ) < 0;
}

QString ImageViewer::classDetailText(quint16 cls)
{
    QString html;
    QTextStream out(&html);

    const quint16 clscls = d_om->fetchClassOf(cls);

    out << "<html>";
    out << "<h2>" << d_om->fetchClassName(cls) << "</h2>";
    out << "<b>class:</b> <a href=\"oop:" << QString::number(clscls,16) << "\">" << d_om->fetchClassName(clscls) << "</a><br>";
    const quint16 super = d_om->fetchPointerOfObject(0,cls);
    out << "<b>superclass:</b> <a href=\"oop:" << QString::number(super,16) << "\">" << d_om->fetchClassName(super) << "</a><br>";

    const quint16 spec = d_om->fetchWordOfObject(2,cls);
    out << "<b>format: </b>";
    if( spec & 0x8000 )
        out << "pointers ";
    if( spec & 0x4000 )
        out << "words ";
    if( spec & 0x2000 )
        out << "indexable ";
    out << ( ( spec >> 1 ) & 0x7ff ) << " fixed fields";
    out << "<br>";

    const quint16 vars = d_om->fetchPointerOfObject(4,cls);
    if( vars != ObjectMemory::objectNil )
    {
        out << "<h3>Fields</h3>";
        const quint16 len = d_om->fetchWordLenghtOf(vars);
        for( int i = 0; i < len; i++ )
        {
            const quint16 str = d_om->fetchPointerOfObject(i,vars);
            out << (const char*) d_om->fetchByteString(str).d_bytes << "<br>";
        }
    }

    const quint16 md = d_om->fetchPointerOfObject(1,cls);
    const quint16 arr = d_om->fetchPointerOfObject(1,md);
    const int len = d_om->fetchWordLenghtOf(arr);
    Q_ASSERT( d_om->fetchWordLenghtOf(md) - 2 == len );

    QList< QPair< QString, quint16 > > list;
    for( int i = 0; i < len; i++ )
    {
        const quint16 meth = d_om->fetchPointerOfObject(i,arr);
        if( meth == ObjectMemory::objectNil )
            continue;
        const quint16 sym = d_om->fetchPointerOfObject(i+2,md);
        list << qMakePair( QString( (const char*)d_om->fetchByteString(sym).d_bytes ), meth );
    }
    if( !list.isEmpty() )
    {
        out << "<h3>Methods</h3><table>";
        std::sort( list.begin(), list.end(), lessThan );
        for( int i = 0; i < list.size(); i++ )
            out << "<tr><td>" << list[i].first.toHtmlEscaped() << "</td><td><a href=\"oop:"
                << QString::number(list[i].second,16) << "\">"
                << QByteArray::number(list[i].second,16) << "</a></td></tr>";
        out << "</table>";
    }

    out << "</html>";

    return html;
}

QString ImageViewer::methodDetailText(quint16 oop)
{
    QString html;
    QTextStream out(&html);

    out << "<html>";
    out << "<h2>Method " << QString::number(oop,16) << "</h2>";
    const quint16 args = d_om->methodArgumentCount(oop);
    out << "<b>arguments:</b> " << args << "<br>";
    out << "<b>temporaries:</b> " << ( d_om->methodTemporaryCount(oop) - args ) << "<br>";
    const quint16 prim = d_om->methodPrimitiveIndex(oop);
    const quint16 flags = d_om->methodFlags(oop);
    if( prim )
        out << "<b>primitive:</b> " << prim << "<br>";
    else if( flags == ObjectMemory::ZeroArgPrimitiveReturnSelf )
        out << "<b>primitive:</b> return self<br>";
    else if( flags == ObjectMemory::ZeroArgPrimitiveReturnVar )
        out << "<b>primitive:</b> return field " << d_om->methodPrimitiveIndex(oop) << "<br>";

    const quint16 len = d_om->methodLiteralCount(oop);
    if( len > 0 )
    {
        out << "<h3>Literals</h3>";
        out << "<table width=100% align=left border=1 cellspacing=0 cellpadding=3>";
        out << "<tr><th>Index</th><th>Value</th></tr>";
        for( int i = 0; i < len; i++ )
        {
            out << "<tr><td>" << i << "</td> <td>";
            quint16 val = d_om->methodLiteral(i, oop);
            out << prettyValue(val);
            out << "</td></tr>";
        }
        out << "</table>";
    }

    ObjectMemory::ByteString bs = d_om->methodBytecodes(oop);
    if( bs.d_len > 0 )
    {
        out << "<h3>Bytecode</h3>";
        out << "<table width=100% align=left cellspacing=0 cellpadding=3>";
        int pc = 0;
        while( pc < bs.d_len )
        {
            QPair<QString,int> res = bytecodeText(bs.d_bytes, pc);
            out << "<tr><td>";
            out << QString("%1").arg(bs.d_bytes[pc],3,10,QChar('0'));
            for( int i = 1; i < res.second; i++ )
                out << "<br>" << QString("%1").arg(bs.d_bytes[pc+i],3,10,QChar('0'));
            out << "</td><td>";
            out << res.first;
            pc += res.second;
            out << "</td></tr>";
        }
        out << "</table>";
    }

    out << "</html>";

    return html;
}

QByteArrayList ImageViewer::fieldList(quint16 cls, bool recursive)
{
    QByteArrayList res;
    if( recursive )
    {
        const quint16 super = d_om->fetchPointerOfObject(0,cls);
        if( super != ObjectMemory::objectNil )
            res = fieldList(super,recursive);
    }
    const quint16 vars = d_om->fetchPointerOfObject(4,cls);
    if( vars != ObjectMemory::objectNil )
    {
        const quint16 len = d_om->fetchWordLenghtOf(vars);
        for( int i = 0; i < len; i++ )
        {
            const quint16 str = d_om->fetchPointerOfObject(i,vars);
            res << (const char*) d_om->fetchByteString(str).d_bytes;
        }
    }
    return res;
}

QByteArray ImageViewer::prettyValue(quint16 val)
{
    const quint16 cls = d_om->fetchClassOf(val);
    switch( cls )
    {
    case ObjectMemory::classSmallInteger:
        return QByteArray::number(ObjectMemory::toInt(val));
        break;
    case ObjectMemory::classCharacter:
        {
            quint16 ch = d_om->fetchWordOfObject(0,val);
            ch = ch >> 1;
            if( ::isprint(ch) )
                return "'" + QByteArray(1,ch) + "'";
            else
                return "0x" + QByteArray::number(ch,16);
        }
        break;
    case ObjectMemory::classSymbol:
    case ObjectMemory::classString:
        return "\"" + QByteArray((const char*)d_om->fetchByteString(val).d_bytes) + "\"";
        break;
    default:
        if( d_mdl->d_knowns.contains(val) )
            return "<a href=\"oop:" + QByteArray::number(val,16) + "\">" + d_mdl->d_knowns.value(val) + "</a>";
        else
            return "<a href=\"oop:" + QByteArray::number(val,16) + "\">" + QByteArray::number(val,16) + "</a>";
        break;
    }
}

void ImageViewer::syncClasses(quint16 oop)
{
    for( int i = 0; i < d_classes->topLevelItemCount(); i++ )
    {
        QTreeWidgetItem* item = d_classes->topLevelItem(i);
        if( item->data(0,Qt::UserRole).toUInt() == oop )
        {
            d_classes->setCurrentItem(item);
            d_classes->scrollToItem(item);
            return;
        }
    }
}

void ImageViewer::syncObjects(quint16 oop)
{
    QModelIndex i = d_mdl->findValue( oop );
    if( i.isValid() )
    {
        d_tree->setCurrentIndex(i);
        d_tree->scrollTo(i);
    }
}

QPair<QString, int> ImageViewer::bytecodeText(const quint8* bc, int pc)
{
    // http://www.mirandabanda.org/bluebook/bluebook_chapter28.html
    const quint8 b = bc[pc];
    if( b >= 0 && b <= 15 )
        return qMakePair( QString("Push Receiver Variable #%1").arg( b & 0xf ), 1 );
    if( b >= 16 && b <= 31 )
        return qMakePair( QString("Push Temporary Location #%1").arg( b & 0xf ), 1 );
    if( b >= 32 && b <= 63 )
        return qMakePair( QString("Push Literal Constant #%1").arg( b & 0x1f ), 1 );
    if( b >= 64 && b <= 95 )
        return qMakePair( QString("Push Literal Variable #%1").arg( b & 0x1f ), 1 );
    if( b >= 96 && b <= 103 )
        return qMakePair( QString("Pop and Store Receiver Variable #%1").arg( b & 0x7 ), 1 );
    if( b >= 104 && b <= 111 )
        return qMakePair( QString("Pop and Store Temporary Location #%1").arg( b & 0x7 ), 1 );
    if( b >= 112 && b <= 119 )
        return qMakePair( QString("Push (receiver, true, false, nil, -1, 0, 1, 2) [%1]").arg( b & 0x7 ), 1 );
    if( b >= 120 && b <= 123 )
        return qMakePair( QString("Return (receiver, true, false, nil) [%1] From Message").arg( b & 0x3 ), 1 );
    if( b >= 124 && b <= 125 )
        return qMakePair( QString("Return Stack Top From (Message, Block) [%1]").arg( b & 0x1 ), 1 );
    if( b >= 126 && b <= 127 )
        return qMakePair( QString("unused" ), 1 );
    if( b == 128 )
        return qMakePair( QString("Push (Receiver Variable, Temporary Location, Literal Constant, Literal Variable) [%1] #%2").
                          arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    if( b == 129 )
        return qMakePair( QString("Store (Receiver Variable, Temporary Location, Illegal, Literal Variable) [%1] #%2").
                          arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    if( b == 130 )
        return qMakePair( QString("Pop and Store (Receiver Variable, Temporary Location, Illegal, Literal Variable) [%1] #%2").
                          arg( ( bc[pc+1] >> 6 ) & 0x3 ).arg( bc[pc+1] & 0x3f), 2 );
    if( b == 131 )
        return qMakePair( QString("Send Literal Selector #%2 With %1 Arguments").
                          arg( ( bc[pc+1] >> 5 ) & 0x7 ).arg( bc[pc+1] & 0x1f), 2 );
    if( b == 132 )
        return qMakePair( QString("Send Literal Selector #%2 With %1 Arguments").
                          arg( bc[pc+1] ).arg( bc[pc+2]), 3 );
    if( b == 133 )
        return qMakePair( QString("Send Literal Selector #%2 To Superclass With %1 Arguments").
                          arg( ( bc[pc+1] >> 5 ) & 0x7 ).arg( bc[pc+1] & 0x1f), 2 );
    if( b == 134 )
        return qMakePair( QString("Send Literal Selector #%2 To Superclass With %1 Arguments").
                          arg( bc[pc+1] ).arg( bc[pc+2]), 3 );
    if( b == 135 )
        return qMakePair( QString("Pop Stack Top" ), 1 );
    if( b == 136 )
        return qMakePair( QString("Duplicate Stack Top" ), 1 );
    if( b == 137 )
        return qMakePair( QString("Push Active Context" ), 1 );
    if( b >= 138 && b <= 143 )
        return qMakePair( QString("unused" ), 1 );
    if( b >= 144 && b <= 151 )
        return qMakePair( QString("Jump %1 + 1 (i.e., 1 through 8)").arg( b & 0x7 ), 1 );
    if( b >= 152 && b <= 159 )
        return qMakePair( QString("Pop and Jump 0n False %1 +1 (i.e., 1 through 8)").arg( b & 0x7 ), 1 );
    if( b >= 160 && b <= 167 )
        return qMakePair( QString("Jump(%1 - 4) *256+%2").arg( b & 0x7 ).arg( bc[pc+1] ), 2 );
    if( b >= 168 && b <= 171 )
        return qMakePair( QString("Pop and Jump On True %1 *256+%2").arg( b & 0x3 ).arg( bc[pc+1] ), 2 );
    if( b >= 172 && b <= 175 )
        return qMakePair( QString("Pop and Jump On False %1 *256+%2").arg( b & 0x3 ).arg( bc[pc+1] ), 2 );
    if( b >= 176 && b <= 191 )
        // see array 0x30 specialSelectors
        return qMakePair( QString("Send Arithmetic Message #%1" ).arg( b & 0xf ), 1 );
    if( b >= 192 && b <= 207 )
        // see array 0x30 specialSelectors
        return qMakePair( QString("Send Special Message #%1" ).arg( b & 0xf ), 1 );
    if( b >= 208 && b <= 223 )
        return qMakePair( QString("Send Literal Selector #%1 With No Arguments" ).arg( b & 0xf ), 1 );
    if( b >= 224 && b <= 239 )
        return qMakePair( QString("Send Literal Selector #%1 With 1 Argument" ).arg( b & 0xf ), 1 );
    if( b >= 240 && b <= 255 )
        return qMakePair( QString("Send Literal Selector #%1 With 2 Arguments" ).arg( b & 0xf ), 1 );

    Q_ASSERT( false );

    return qMakePair(QString(),1);
}

void ImageViewer::pushLocation(quint16 oop)
{
    if( d_pushBackLock )
        return;
    if( !d_backHisto.isEmpty() && d_backHisto.last() == oop )
        return; // o ist bereits oberstes Element auf dem Stack.
    d_backHisto.removeAll( oop );
    d_backHisto.push_back( oop );
}

void ImageViewer::onObject(quint16 oop)
{
    if( QApplication::keyboardModifiers() == Qt::ControlModifier )
    {
        QDockWidget* dock = new QDockWidget( tr("oop %1").arg(oop,0,16), this );
        dock->setObjectName("OopX");
        dock->setAllowedAreas( Qt::AllDockWidgetAreas );
        dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
        dock->setAttribute(Qt::WA_DeleteOnClose);
        ObjectTree* tv = new ObjectTree();
        ImageViewer::Model* m = new ImageViewer::Model(tv);
        tv->setModel(m);
        m->setOm( d_om, oop);
        tv->expandToDepth(0);
        dock->setWidget(tv);
        addDockWidget( Qt::RightDockWidgetArea, dock );
        connect( tv, SIGNAL(sigObject(quint16)), this, SLOT(onObject(quint16)) );
    }else
    {
        showDetail(oop);
        syncClasses(oop);
        fillXref(oop);
        pushLocation(oop);
    }
}

void ImageViewer::onClassesClicked()
{
    QTreeWidgetItem* item = d_classes->currentItem();
    if( item == 0 )
        return;

    const quint16 oop = item->data(0,Qt::UserRole).toUInt();
    syncObjects(oop);
    showDetail(oop);
    fillXref(oop);
    pushLocation(oop);
}

void ImageViewer::onLink(const QUrl& url)
{
    if( url.scheme() != "oop" )
        return;
    const QString str = url.path();
    quint16 oop = str.toUInt(0,16);

    if( QApplication::keyboardModifiers() == Qt::ControlModifier )
    {
        QDockWidget* dock = new QDockWidget( tr("oop %1 detail").arg(oop,0,16), this );
        dock->setObjectName("OopDetailX");
        dock->setAllowedAreas( Qt::AllDockWidgetAreas );
        dock->setFeatures( QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable );
        dock->setAttribute(Qt::WA_DeleteOnClose);
        QTextBrowser* tb = new QTextBrowser(dock);
        tb->setOpenLinks(false);
        dock->setWidget(tb);
        addDockWidget( Qt::RightDockWidgetArea, dock );
        connect( tb, SIGNAL(anchorClicked(QUrl)), this, SLOT(onLink(QUrl)) );
        tb->setHtml( detailText(oop) );
    }else
    {
        showDetail(oop);
        syncClasses(oop);
        syncObjects(oop);
        fillXref(oop);
        pushLocation(oop);
    }
}

void ImageViewer::onLink(const QString& link)
{
    if( !link.startsWith( "oop:" ) )
        return;
    quint16 oop = link.mid(4).toUInt(0,16);
    showDetail(oop);
    syncClasses(oop);
    syncObjects(oop);
    fillXref(oop);
    pushLocation(oop);
}

void ImageViewer::onGoBack()
{
    if( d_backHisto.size() <= 1 )
        return;

    d_pushBackLock = true;
    d_forwardHisto.push_back( d_backHisto.last() );
    d_backHisto.pop_back();
    const quint16 oop = d_backHisto.last();
    showDetail(oop);
    syncClasses(oop);
    syncObjects(oop);
    fillXref(oop);

    d_pushBackLock = false;
}

void ImageViewer::onGoForward()
{
    if( d_forwardHisto.isEmpty() )
        return;
    quint16 oop = d_forwardHisto.last();
    d_forwardHisto.pop_back();
    showDetail(oop);
    syncClasses(oop);
    syncObjects(oop);
    fillXref(oop);
    pushLocation(oop);
}

void ImageViewer::onXrefClicked(QTreeWidgetItem* item, int col)
{
    quint16 oop = item->data(col,Qt::UserRole).toUInt();
    showDetail(oop);
    syncClasses(oop);
    syncObjects(oop);
    fillXref(oop);
    pushLocation(oop);
}

ObjectTree::ObjectTree(QWidget* p)
{
    setAlternatingRowColors(true);
    setExpandsOnDoubleClick(false);
    connect( this, SIGNAL(clicked(QModelIndex)), this, SLOT(onClicked(QModelIndex)) );
}

void ObjectTree::setModel(ImageViewer::Model* m)
{
    d_mdl = m;
    QTreeView::setModel(m);
}

void ObjectTree::onClicked(const QModelIndex& index)
{
    quint16 oop = d_mdl->getValue(index);
    if( index.column() == 1 )
        oop = d_mdl->d_om->fetchClassOf(oop);

    emit sigObject(oop);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName("me@rochus-keller.ch");
    a.setOrganizationDomain("github.com/rochus-keller/Smalltalk");
    a.setApplicationName("Smalltalk 80 Image Viewer");
    a.setApplicationVersion("0.6");
    a.setStyle("Fusion");

    ImageViewer w;

    if( a.arguments().size() > 1 )
        w.parse( a.arguments()[1] );
    else
    {
        const QString path = QFileDialog::getOpenFileName(&w,ImageViewer::tr("Open Smalltalk-80 Image File"),
                                                          QString(), "VirtualImage *.image *.im" );
        if( path.isEmpty() )
            return 0;
        w.parse(path);
    }
    return a.exec();
}
