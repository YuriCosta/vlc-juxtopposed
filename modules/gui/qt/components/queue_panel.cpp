/*****************************************************************************
 * queue_panel.cpp : floating play queue of the redesigned interface
 ****************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "components/queue_panel.hpp"
#include "components/playlist/playlist_model.hpp"
#include "components/playlist/sorting.h"
#include "input_manager.hpp"
#include "dialogs_provider.hpp"
#include "recents.hpp"                  /* Open::openMRL */
#include "adapters/seekpoints.hpp"
#include "util/redesign.hpp"

#include <vlc_playlist.h>

#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QMimeData>
#include <QTimer>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#define PANEL_WIDTH  400
#define PANEL_MARGIN 20  /* distance to the top bar, the control bar and the window side */
#define PANEL_RADIUS 10

namespace {

/* One queue entry: [drag handle] title ............ duration */
class QueueItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint( QPainter *painter, const QStyleOptionViewItem &option,
                const QModelIndex &index ) const Q_DECL_OVERRIDE
    {
        painter->save();
        painter->setRenderHint( QPainter::Antialiasing );
        const QRect r = option.rect;

        if( option.state & ( QStyle::State_Selected | QStyle::State_MouseOver ) )
        {
            painter->setPen( Qt::NoPen );
            painter->setBrush( option.state & QStyle::State_Selected
                               ? Redesign::DividerBright : Redesign::DividerDim );
            painter->drawRoundedRect( r, 4, 4 );
        }

        static const QIcon dragIcon( ":/redesign/queue-drag.svg" );
        dragIcon.paint( painter, QRect( r.left() + 4, r.center().y() - 11, 24, 24 ) );

        const bool b_current = index.data( VLCModelSubInterface::CURRENT_ITEM_ROLE ).toBool();
        QFont font = option.font;
        font.setPixelSize( 14 );
        painter->setFont( font );
        const QFontMetrics fm( font );

        const QString duration = VLCModel::getMeta( index, COLUMN_DURATION );
        const int durationWidth = duration.isEmpty() ? 0 : fm.width( duration );
        const QRect durationRect( r.right() - 4 - durationWidth, r.top(), durationWidth, r.height() );
        const QRect titleRect( r.left() + 42, r.top(),
                               durationRect.left() - 10 - ( r.left() + 42 ), r.height() );

        painter->setPen( b_current ? Redesign::Accent : Redesign::FgA1 );
        painter->drawText( titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                           fm.elidedText( index.data( Qt::DisplayRole ).toString(),
                                          Qt::ElideRight, titleRect.width() ) );
        painter->setPen( Redesign::FgA3 );
        painter->drawText( durationRect, Qt::AlignVCenter | Qt::AlignRight, duration );
        painter->restore();
    }

    QSize sizeHint( const QStyleOptionViewItem &option, const QModelIndex & ) const Q_DECL_OVERRIDE
    {
        return QSize( option.rect.width(), 32 );
    }
};

/* "1  Winter ........ 00:02:15": bookmarks and chapters */
class TimedItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint( QPainter *painter, const QStyleOptionViewItem &option,
                const QModelIndex &index ) const Q_DECL_OVERRIDE
    {
        painter->save();
        painter->setRenderHint( QPainter::Antialiasing );
        const QRect r = option.rect;
        if( option.state & ( QStyle::State_Selected | QStyle::State_MouseOver ) )
        {
            painter->setPen( Qt::NoPen );
            painter->setBrush( option.state & QStyle::State_Selected
                               ? Redesign::DividerBright : Redesign::DividerDim );
            painter->drawRoundedRect( r, 4, 4 );
        }
        QFont font = option.font;
        font.setPixelSize( 14 );
        painter->setFont( font );
        const QFontMetrics fm( font );

        int left = r.left() + 14;
        const QString number = index.data( Qt::UserRole + 2 ).toString();
        if( !number.isEmpty() )
        {
            painter->setPen( Redesign::Accent );
            painter->drawText( QRect( left, r.top(), 24, r.height() ), Qt::AlignVCenter | Qt::AlignLeft, number );
            left += 28;
        }
        const QString time = index.data( Qt::UserRole + 1 ).toString();
        const int timeWidth = fm.width( time );
        painter->setPen( Redesign::FgA3 );
        painter->drawText( QRect( r.right() - 4 - timeWidth, r.top(), timeWidth, r.height() ),
                           Qt::AlignVCenter | Qt::AlignRight, time );
        const QRect titleRect( left, r.top(), r.right() - 14 - timeWidth - left, r.height() );
        painter->setPen( Redesign::FgA1 );
        painter->drawText( titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                           fm.elidedText( index.data().toString(), Qt::ElideRight, titleRect.width() ) );
        painter->restore();
    }

    QSize sizeHint( const QStyleOptionViewItem &option, const QModelIndex & ) const Q_DECL_OVERRIDE
    {
        return QSize( option.rect.width(), 32 );
    }
};

QString formatTime( int64_t t, bool b_millis )
{
    const int64_t ms = t / 1000;
    QString text = QString( "%1:%2:%3" ).arg( ms / 3600000, 2, 10, QChar( '0' ) )
                                        .arg( ( ms / 60000 ) % 60, 2, 10, QChar( '0' ) )
                                        .arg( ( ms / 1000 ) % 60, 2, 10, QChar( '0' ) );
    if( b_millis )
        text += QString( ":%1" ).arg( ms % 1000, 3, 10, QChar( '0' ) );
    return text;
}

QString formatTotalDuration( mtime_t total )
{
    const int seconds = (int)( total / CLOCK_FREQ );
    const int minutes = seconds / 60;
    if( minutes >= 60 )
        return qtr( "%1h %2m" ).arg( minutes / 60 ).arg( minutes % 60 );
    if( minutes > 0 )
        return qtr( "%1m" ).arg( minutes );
    return qtr( "%1s" ).arg( seconds );
}

} // namespace

QueuePanel::QueuePanel( intf_thread_t *_p_intf, QWidget *parent, QWidget *_area )
           : QFrame( parent ), p_intf( _p_intf ), area( _area )
{
    /* Native, so that it can be stacked above the (native) video output */
    setAttribute( Qt::WA_NativeWindow );
    setFocusPolicy( Qt::StrongFocus );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    /* Tabs: what plays next, the user's bookmarks, the chapters of the media */
    QWidget *tabBar = new QWidget;
    tabBar->setObjectName( "queueTabs" );
    tabBar->setFixedHeight( 50 );
    QHBoxLayout *tabsLayout = new QHBoxLayout( tabBar );
    tabsLayout->setContentsMargins( 6, 0, 6, 0 );
    tabsLayout->setSpacing( 1 );
    tabs = new QButtonGroup( this );
    const struct { const char *icon; const char *text; } tabList[] = {
        { ":/redesign/tab-queue.svg",     N_( "Queue" )     },
        { ":/redesign/tab-bookmarks.svg", N_( "Bookmarks" ) },
        { ":/redesign/tab-chapters.svg",  N_( "Chapters" )  },
    };
    for( const auto &t : tabList )
    {
        QToolButton *tab = new QToolButton;
        tab->setObjectName( "queueTab" );
        tab->setProperty( "active", tabs->buttons().isEmpty() );
        tab->setIcon( QIcon( t.icon ) );
        tab->setIconSize( QSize( 20, 20 ) );
        tab->setText( qtr( t.text ) );
        tab->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        tab->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
        tab->setFocusPolicy( Qt::NoFocus );
        tabs->addButton( tab, tabs->buttons().count() );
        tabsLayout->addWidget( tab );
    }
    connect( tabs, static_cast<void (QButtonGroup::*)(int)>( &QButtonGroup::buttonClicked ),
             this, &QueuePanel::showTab );
    layout->addWidget( tabBar );

    pages = new QStackedWidget;
    layout->addWidget( pages, 1 );
    QWidget *queuePage = new QWidget;
    QVBoxLayout *queueLayout = new QVBoxLayout( queuePage );
    queueLayout->setContentsMargins( 0, 0, 0, 0 );
    queueLayout->setSpacing( 0 );
    pages->addWidget( queuePage );

    /* Summary and actions */
    QHBoxLayout *header = new QHBoxLayout;
    header->setContentsMargins( 28, 20, 20, 20 );
    header->setSpacing( 12 );
    summaryLabel = new QLabel;
    summaryLabel->setObjectName( "queueSummary" );
    header->addWidget( summaryLabel, 1 );

    QToolButton *sortButton = createButton( ":/redesign/queue-sort.svg", qtr( "Sort" ) );
    connect( sortButton, &QToolButton::clicked, this, &QueuePanel::showSortMenu );
    header->addWidget( sortButton );

    repeatButton = createButton( ":/redesign/queue-repeat.svg", qtr( "Repeat" ) );
    repeatButton->setCheckable( true );
    connect( repeatButton, &QToolButton::clicked, THEMIM, &MainInputManager::loopRepeatLoopStatus );
    connect( THEMIM, &MainInputManager::repeatLoopChanged, this, &QueuePanel::setRepeat );
    header->addWidget( repeatButton );

    shuffleButton = createButton( ":/redesign/queue-shuffle.svg", qtr( "Random" ) );
    shuffleButton->setCheckable( true );
    connect( shuffleButton, &QToolButton::clicked, THEMIM, &MainInputManager::toggleRandom );
    connect( THEMIM, &MainInputManager::randomChanged, this, &QueuePanel::setRandom );
    header->addWidget( shuffleButton );

    QToolButton *removeButton = createButton( ":/redesign/queue-remove.svg", qtr( "Remove selected items" ) );
    connect( removeButton, &QToolButton::clicked, this, &QueuePanel::removeSelected );
    header->addWidget( removeButton );

    QToolButton *addButton = createButton( ":/redesign/queue-add.svg", qtr( "Add files to the queue" ) );
    connect( addButton, &QToolButton::clicked, this, []() { THEDP->PLAppendDialog(); } );
    header->addWidget( addButton );
    queueLayout->addLayout( header );

    /* The queue itself */
    model = PLModel::getPLModel( p_intf );
    view = new QListView;
    view->setObjectName( "queueView" );
    view->setModel( model );
    view->setRootIndex( model->rootIndex() );
    view->setModelColumn( VLCModel::metaToColumn( COLUMN_TITLE ) );
    view->setItemDelegate( new QueueItemDelegate( view ) );
    view->setSpacing( 6 ); /* 12px between rows */
    view->setFrameShape( QFrame::NoFrame );
    view->setSelectionMode( QAbstractItemView::ExtendedSelection );
    view->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view->setDragEnabled( true );
    view->setAcceptDrops( true );
    view->setDropIndicatorShown( true );
    view->setDragDropMode( QAbstractItemView::DragDrop );
    view->setDefaultDropAction( Qt::MoveAction );
    view->viewport()->setAttribute( Qt::WA_Hover );
    connect( view, &QListView::activated, this, &QueuePanel::activate );
    /* Files dragged from the library or the file manager are enqueued */
    view->viewport()->installEventFilter( this );

    QVBoxLayout *listLayout = new QVBoxLayout;
    listLayout->setContentsMargins( 14, 0, 14, 14 );
    listLayout->addWidget( view );
    queueLayout->addLayout( listLayout, 1 );

    /* Bookmarks and chapters: "name ........ time", activation seeks */
    auto makeTimedPage = [this]( QLabel **summary, QListView **list, QStandardItemModel **listModel,
                                 QHBoxLayout **headerOut ) {
        QWidget *page = new QWidget;
        QVBoxLayout *pageLayout = new QVBoxLayout( page );
        pageLayout->setContentsMargins( 0, 0, 0, 0 );
        pageLayout->setSpacing( 0 );
        QHBoxLayout *pageHeader = new QHBoxLayout;
        pageHeader->setContentsMargins( 28, 20, 20, 20 );
        pageHeader->setSpacing( 12 );
        *summary = new QLabel;
        (*summary)->setObjectName( "queueSummary" );
        pageHeader->addWidget( *summary, 1 );
        pageLayout->addLayout( pageHeader );
        *listModel = new QStandardItemModel( this );
        *list = new QListView;
        (*list)->setObjectName( "queueView" );
        (*list)->setModel( *listModel );
        (*list)->setItemDelegate( new TimedItemDelegate( *list ) );
        (*list)->setSpacing( 6 );
        (*list)->setFrameShape( QFrame::NoFrame );
        (*list)->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        (*list)->setEditTriggers( QAbstractItemView::NoEditTriggers );
        (*list)->viewport()->setAttribute( Qt::WA_Hover );
        QVBoxLayout *pageList = new QVBoxLayout;
        pageList->setContentsMargins( 14, 0, 14, 14 );
        pageList->addWidget( *list );
        pageLayout->addLayout( pageList, 1 );
        pages->addWidget( page );
        *headerOut = pageHeader;
    };

    QHBoxLayout *bookmarksHeader;
    makeTimedPage( &bookmarksSummary, &bookmarksView, &bookmarksModel, &bookmarksHeader );
    QToolButton *delBookmark = createButton( ":/redesign/queue-remove.svg", qtr( "Delete the selected bookmark" ) );
    connect( delBookmark, &QToolButton::clicked, this, [this]() {
        input_thread_t *p_input = THEMIM->getInput();
        const QModelIndex index = bookmarksView->currentIndex();
        if( p_input && index.isValid() )
            input_Control( p_input, INPUT_DEL_BOOKMARK, index.row() );
        updateBookmarks();
    } );
    bookmarksHeader->addWidget( delBookmark );
    QToolButton *addBookmark = createButton( ":/redesign/queue-add.svg", qtr( "Bookmark the current time" ) );
    connect( addBookmark, &QToolButton::clicked, this, [this]() {
        input_thread_t *p_input = THEMIM->getInput();
        seekpoint_t bookmark;
        if( p_input && !input_Control( p_input, INPUT_GET_BOOKMARK, &bookmark ) )
        {
            const QByteArray name = QString( THEMIM->getIM()->getName() + " "
                                    + QString::number( bookmarksModel->rowCount() + 1 ) ).toUtf8();
            bookmark.psz_name = const_cast<char *>( name.constData() );
            input_Control( p_input, INPUT_ADD_BOOKMARK, &bookmark );
        }
        updateBookmarks();
    } );
    bookmarksHeader->addWidget( addBookmark );
    connect( bookmarksView, &QListView::activated, this, [this]( const QModelIndex &index ) {
        input_thread_t *p_input = THEMIM->getInput();
        if( p_input )
            input_Control( p_input, INPUT_SET_BOOKMARK, index.row() );
    } );
    connect( THEMIM->getIM(), &InputManager::bookmarksChanged, this, &QueuePanel::updateBookmarks );

    QHBoxLayout *chaptersHeader;
    makeTimedPage( &chaptersSummary, &chaptersView, &chaptersModel, &chaptersHeader );
    seekPoints = new SeekPoints( this, p_intf );
    connect( chaptersView, &QListView::activated, this, [this]( const QModelIndex &index ) {
        seekPoints->jumpTo( index.row() );
    } );
    connect( THEMIM->getIM(), &InputManager::chapterChanged, this, &QueuePanel::updateChapters );
    connect( THEMIM, &MainInputManager::inputChanged, this, [this]() {
        if( isVisible() ) { updateBookmarks(); updateChapters(); }
    } );

    connect( model, &QAbstractItemModel::rowsInserted, this, &QueuePanel::updateSummary );
    connect( model, &QAbstractItemModel::rowsRemoved, this, &QueuePanel::updateSummary );
    connect( model, &QAbstractItemModel::modelReset, this, &QueuePanel::updateSummary );
    connect( model, &QAbstractItemModel::dataChanged, this, &QueuePanel::updateSummary );
    connect( model, &VLCModel::rootIndexChanged, this, [this]() {
        view->setRootIndex( model->rootIndex() );
        updateSummary();
    } );

    /* Initial states */
    setRandom( var_GetBool( THEPL, "random" ) );
    setRepeat( var_GetBool( THEPL, "repeat" ) ? REPEAT_ONE
             : var_GetBool( THEPL, "loop" ) ? REPEAT_ALL : NORMAL );
    updateSummary();

    area->installEventFilter( this );
    QTimer *watcher = Redesign::outsideClickWatcher( this );
    connect( this, &QueuePanel::visibilityChanged, watcher,
             [watcher]( bool b ) { if( b ) watcher->start(); else watcher->stop(); } );
    hide();
}

/* Never let the wheel reach the main window, which turns it into volume */
void QueuePanel::wheelEvent( QWheelEvent *event )
{
    event->accept();
}

QToolButton *QueuePanel::createButton( const QString &icon, const QString &tooltip )
{
    QToolButton *button = new QToolButton;
    button->setIcon( QIcon( icon ) );
    button->setIconSize( QSize( 24, 24 ) );
    button->setFixedSize( QSize( 28, 28 ) );
    button->setToolTip( tooltip );
    button->setFocusPolicy( Qt::NoFocus );
    button->setAutoRaise( true );
    return button;
}

void QueuePanel::updateSummary()
{
    const QModelIndex root = model->rootIndex();
    const int count = model->rowCount( root );
    mtime_t total = 0;
    for( int i = 0; i < count; i++ )
    {
        input_item_t *p_item = model->getInputItem( model->index( i, 0, root ) );
        const mtime_t duration = p_item ? input_item_GetDuration( p_item ) : 0;
        if( duration > 0 )
            total += duration;
    }

    QString text = count == 1 ? qtr( "1 item" ) : qtr( "%1 items" ).arg( count );
    if( total > 0 )
        text += " - " + formatTotalDuration( total );
    summaryLabel->setText( text );
}

void QueuePanel::updateGeometryFromArea()
{
    if( !parentWidget() )
        return;
    const QRect a( area->mapTo( parentWidget(), QPoint( 0, 0 ) ), area->size() );
    const int w = qMin( PANEL_WIDTH, a.width() - 2 * PANEL_MARGIN );
    const int h = a.height() - 2 * PANEL_MARGIN;
    setGeometry( a.right() - PANEL_MARGIN - w + 1, a.top() + PANEL_MARGIN,
                 qMax( w, 200 ), qMax( h, 150 ) );
    raise();
}

bool QueuePanel::eventFilter( QObject *obj, QEvent *event )
{
    if( obj == area && ( event->type() == QEvent::Resize || event->type() == QEvent::Move ) )
        updateGeometryFromArea();

    if( obj == view->viewport() &&
        ( event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
          event->type() == QEvent::Drop ) )
    {
        QDropEvent *drop = static_cast<QDropEvent *>( event );
        const QMimeData *mime = drop->mimeData();
        /* Moves inside the queue are handled by the playlist model */
        if( qobject_cast<const PlMimeData *>( mime ) || !mime->hasUrls() )
            return QFrame::eventFilter( obj, event );

        drop->setDropAction( Qt::CopyAction );
        drop->accept();
        if( event->type() == QEvent::Drop )
            foreach( const QUrl &url, mime->urls() )
                Open::openMRL( p_intf, url.toString(), false, true );
        return true;
    }
    return QFrame::eventFilter( obj, event );
}

void QueuePanel::resizeEvent( QResizeEvent *event )
{
    QFrame::resizeEvent( event );
    /* A native child cannot be translucent: clip the rounded corners */
    QPainterPath path;
    path.addRoundedRect( QRectF( rect() ), PANEL_RADIUS, PANEL_RADIUS );
    setMask( QRegion( path.toFillPolygon().toPolygon() ) );
}

void QueuePanel::showEvent( QShowEvent *event )
{
    QFrame::showEvent( event );
    updateGeometryFromArea();
    updateSummary();
    view->scrollTo( model->currentIndex() );
    view->setFocus();
    updateBookmarks();
    updateChapters();
    emit visibilityChanged( true );
}

void QueuePanel::hideEvent( QHideEvent *event )
{
    QFrame::hideEvent( event );
    emit visibilityChanged( false );
}

void QueuePanel::keyPressEvent( QKeyEvent *event )
{
    if( event->key() == Qt::Key_Delete )
        removeSelected();
    else if( event->key() == Qt::Key_Escape )
        hide();
    else
        QFrame::keyPressEvent( event );
}

void QueuePanel::activate( const QModelIndex &index )
{
    model->activateItem( index );
}

void QueuePanel::removeSelected()
{
    model->doDelete( view->selectionModel()->selectedIndexes() );
}

void QueuePanel::showSortMenu()
{
    QMenu menu( this );
    const int titleColumn = VLCModel::metaToColumn( COLUMN_TITLE );
    const int durationColumn = VLCModel::metaToColumn( COLUMN_DURATION );
    menu.addAction( qtr( "Title (A to Z)" ), this, [=]() { model->sort( titleColumn, Qt::AscendingOrder ); } );
    menu.addAction( qtr( "Title (Z to A)" ), this, [=]() { model->sort( titleColumn, Qt::DescendingOrder ); } );
    menu.addAction( qtr( "Shortest first" ), this, [=]() { model->sort( durationColumn, Qt::AscendingOrder ); } );
    menu.addAction( qtr( "Longest first" ), this, [=]() { model->sort( durationColumn, Qt::DescendingOrder ); } );
    QWidget *button = qobject_cast<QWidget *>( sender() );
    menu.exec( button ? button->mapToGlobal( QPoint( 0, button->height() ) ) : QCursor::pos() );
}

void QueuePanel::showTab( int id )
{
    foreach( QAbstractButton *tab, tabs->buttons() )
    {
        tab->setProperty( "active", tabs->id( tab ) == id );
        tab->style()->unpolish( tab );
        tab->style()->polish( tab );
    }
    pages->setCurrentIndex( id );
    if( id == 1 )
        updateBookmarks();
    else if( id == 2 )
        updateChapters();
}

void QueuePanel::updateBookmarks()
{
    bookmarksModel->clear();
    input_thread_t *p_input = THEMIM->getInput();
    seekpoint_t **pp_bookmarks;
    int i_bookmarks = 0;
    if( p_input && input_Control( p_input, INPUT_GET_BOOKMARKS, &pp_bookmarks, &i_bookmarks ) == VLC_SUCCESS )
    {
        for( int i = 0; i < i_bookmarks; i++ )
        {
            QStandardItem *item = new QStandardItem( qfu( pp_bookmarks[i]->psz_name ) );
            item->setData( formatTime( pp_bookmarks[i]->i_time_offset, true ), Qt::UserRole + 1 );
            bookmarksModel->appendRow( item );
            vlc_seekpoint_Delete( pp_bookmarks[i] );
        }
        free( pp_bookmarks );
    }
    const int count = bookmarksModel->rowCount();
    bookmarksSummary->setText( count == 1 ? qtr( "1 Bookmark" ) : qtr( "%1 Bookmarks" ).arg( count ) );
}

void QueuePanel::updateChapters()
{
    seekPoints->update();
    chaptersModel->clear();
    /* getPoints() locks the list itself and returns a copy */
    int number = 1;
    foreach( const SeekPoint &point, seekPoints->getPoints() )
    {
        QStandardItem *item = new QStandardItem( point.name.isEmpty()
                                                 ? qtr( "Chapter %1" ).arg( number ) : point.name );
        item->setData( formatTime( point.time, false ), Qt::UserRole + 1 );
        item->setData( QString::number( number++ ), Qt::UserRole + 2 );
        chaptersModel->appendRow( item );
    }
    const int count = chaptersModel->rowCount();
    chaptersSummary->setText( count == 0 ? qtr( "This media has no chapters" )
                            : count == 1 ? qtr( "1 Chapter" ) : qtr( "%1 Chapters" ).arg( count ) );
}

void QueuePanel::setRandom( bool b_random )
{
    shuffleButton->setChecked( b_random );
}

void QueuePanel::setRepeat( int state )
{
    repeatButton->setChecked( state != NORMAL );
    repeatButton->setToolTip( state == REPEAT_ONE ? qtr( "Repeat one" )
                            : state == REPEAT_ALL ? qtr( "Repeat all" ) : qtr( "Repeat" ) );
}
