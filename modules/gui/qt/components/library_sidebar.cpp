/*****************************************************************************
 * library_sidebar.cpp : library of the redesigned interface
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

#include "components/library_sidebar.hpp"
#include "input_manager.hpp"
#include "dialogs_provider.hpp"
#include "recents.hpp"
#include "util/redesign.hpp"
#include "util/thumbnailer.hpp"
#include "util/playlists.hpp"
#include "dialogs/new_playlist.hpp"

#include <vlc_input.h>
#include <vlc_interface.h> /* EXTENSIONS_MEDIA */

#include <QButtonGroup>
#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

#define COMPACT_WIDTH        427
#define RAIL_WIDTH           50
#define RAIL_EXPANDED_WIDTH  217

#define MRL_ROLE      ( Qt::UserRole + 10 ) /* QFileSystemModel uses UserRole + 1..3 */
#define FOLDER_ROLE   ( Qt::UserRole + 11 ) /* home page: item is a folder */
#define PLAYLIST_ROLE ( Qt::UserRole + 12 ) /* playlists page: the playlist file */
#define NEW_ROLE      ( Qt::UserRole + 13 ) /* playlists page: "New playlist" */
#define ICON_ROLE     ( Qt::UserRole + 14 ) /* icon when there is no thumbnail */
#define TRACK_ROLE    ( Qt::UserRole + 15 ) /* playlist page: index of the track */

#define PAGE_RECENT    QStringLiteral( "recent:" )
#define PAGE_HOME      QStringLiteral( "home:" )
#define PAGE_PLAYLISTS QStringLiteral( "playlists:" )
#define PAGE_PLAYLIST  QStringLiteral( "playlist:" ) /* followed by the file */

enum
{
    SECTION_HOME,
    SECTION_VIDEOS,
    SECTION_MUSIC,
    SECTION_NETWORK,
    SECTION_PLAYLISTS,
    SECTION_RECENT,
    SECTION_ADD,
};

/* Keeps the folders first and filters the current folder by name */
class LibraryFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFolder( const QString &path )
    {
        folder = QDir::cleanPath( path );
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow( int row, const QModelIndex &parent ) const Q_DECL_OVERRIDE
    {
        QFileSystemModel *fs = qobject_cast<QFileSystemModel *>( sourceModel() );
        if( !fs || filterRegExp().isEmpty() )
            return true;
        /* only filter the children of the displayed folder, never its parents */
        if( QDir::cleanPath( fs->filePath( parent ) ) != folder )
            return true;
        return fs->fileName( fs->index( row, 0, parent ) ).contains( filterRegExp() );
    }

    bool lessThan( const QModelIndex &left, const QModelIndex &right ) const Q_DECL_OVERRIDE
    {
        QFileSystemModel *fs = qobject_cast<QFileSystemModel *>( sourceModel() );
        if( fs )
        {
            const bool leftDir = fs->isDir( left ), rightDir = fs->isDir( right );
            if( leftDir != rightDir )
                return sortOrder() == Qt::AscendingOrder ? leftDir : rightDir;
            /* names sorted naturally (E2 before E10), like next/previous */
            if( sortColumn() == 0 )
            {
                static QCollator collator;
                collator.setNumericMode( true );
                collator.setCaseSensitivity( Qt::CaseInsensitive );
                return collator.compare( fs->fileName( left ), fs->fileName( right ) ) < 0;
            }
        }
        return QSortFilterProxyModel::lessThan( left, right );
    }

private:
    QString folder;
};

namespace {

bool isPage( const QString &location )
{
    return location == PAGE_RECENT || location == PAGE_HOME || location == PAGE_PLAYLISTS
        || location.startsWith( PAGE_PLAYLIST );
}

QString currentMediaPath( intf_thread_t *p_intf )
{
    QString path;
    input_thread_t *p_input = THEMIM->getInput();
    if( p_input )
    {
        input_item_t *p_item = input_GetItem( p_input );
        char *psz_uri = p_item ? input_item_GetURI( p_item ) : NULL;
        if( psz_uri )
        {
            const QUrl url( qfu( psz_uri ) );
            free( psz_uri );
            if( url.isLocalFile() )
                path = QDir::cleanPath( url.toLocalFile() );
        }
    }
    return path;
}

bool isAudioFile( const QString &name )
{
    const QString suffix = QFileInfo( name ).suffix().toLower();
    return !suffix.isEmpty() &&
           QString( QString( EXTENSIONS_AUDIO ) + ";" ).contains( QString( "*." + suffix + ";" ) );
}

/* Item sizes, chosen with the "itemSize" property of the view */
enum ItemSize { ITEM_LIST, ITEM_SMALL, ITEM_LARGE };

/* Figma "media" component: thumbnail (or icon) + name. The playing media is
 * orange, started ones are dimmed with an orange mark under the thumbnail. */
class LibraryItemDelegate : public QStyledItemDelegate
{
public:
    LibraryItemDelegate( intf_thread_t *_p_intf, QListView *_view )
        : QStyledItemDelegate( _view ), p_intf( _p_intf ), view( _view ) {}

    static void drawThumbnail( QPainter *painter, const QRect &rect, const QString &path,
                               bool b_dir, bool b_audio, bool b_started, int radius,
                               const QString &iconPath = QString() )
    {
        static const QIcon folderIcon( ":/redesign/folder.svg" );
        static const QIcon videoIcon( ":/redesign/lib-videos.svg" );
        static const QIcon musicIcon( ":/redesign/lib-music.svg" );

        QPainterPath clip;
        clip.addRoundedRect( QRectF( rect ), radius, radius );
        const QPixmap thumb = b_dir || path.isEmpty() ? QPixmap()
                                                      : Thumbnailer::instance()->thumbnail( path );
        if( !thumb.isNull() )
        {
            const QPixmap scaled = thumb.scaled( rect.size(), Qt::KeepAspectRatioByExpanding,
                                                 Qt::SmoothTransformation );
            painter->save();
            painter->setClipPath( clip );
            painter->drawPixmap( rect.left() - ( scaled.width() - rect.width() ) / 2,
                                 rect.top() - ( scaled.height() - rect.height() ) / 2, scaled );
            painter->restore();
        }
        else
        {
            if( rect.height() > 32 )
                painter->fillPath( clip, Redesign::BgA3 );
            const int s = qMin( 24, rect.height() );
            const QIcon icon = !iconPath.isEmpty() ? QIcon( iconPath )
                             : b_dir ? folderIcon : b_audio ? musicIcon : videoIcon;
            icon.paint( painter, QRect( rect.center().x() - s / 2 + 1, rect.center().y() - s / 2 + 1, s, s ) );
        }
        if( b_started )
            painter->fillRect( QRect( rect.left(), rect.bottom() - 2, rect.width() - 4, 3 ), Redesign::Accent );
    }

    void paint( QPainter *painter, const QStyleOptionViewItem &option,
                const QModelIndex &index ) const Q_DECL_OVERRIDE
    {
        painter->save();
        painter->setRenderHint( QPainter::Antialiasing );
        painter->setRenderHint( QPainter::SmoothPixmapTransform );
        const QRect r = option.rect;
        const int size = view->property( "itemSize" ).toInt();

        if( option.state & ( QStyle::State_Selected | QStyle::State_MouseOver ) )
        {
            painter->setPen( Qt::NoPen );
            painter->setBrush( option.state & QStyle::State_Selected
                               ? Redesign::DividerBright : Redesign::DividerDim );
            painter->drawRoundedRect( r, 4, 4 );
        }

        const QString name = index.data().toString();
        const QString path = index.data( QFileSystemModel::FilePathRole ).toString();
        const QString mrl = index.data( MRL_ROLE ).toString();
        const QString target = !mrl.isEmpty() ? mrl : QUrl::fromLocalFile( path ).toString();
        const QString iconPath = index.data( ICON_ROLE ).toString();
        /* playlists show the thumbnail of their image or first track */
        const bool b_playlist = !index.data( PLAYLIST_ROLE ).toString().isEmpty()
                             || index.data( NEW_ROLE ).toBool();
        const bool b_dir = index.data( FOLDER_ROLE ).toBool()
                        || ( mrl.isEmpty() && !b_playlist && QFileInfo( path ).isDir() );
        const bool b_current = !b_playlist && !path.isEmpty()
                            && QDir::cleanPath( path ) == currentMediaPath( p_intf );
        const bool b_started = !b_dir && !b_playlist && RecentsMRL::getInstance( p_intf )->time( target ) > 0;

        QFont font = option.font;
        font.setPixelSize( 14 );
        painter->setFont( font );
        const QFontMetrics fm( font );
        painter->setPen( b_current ? Redesign::Accent
                         : ( b_dir || b_started ) ? Redesign::FgA3 : Redesign::FgA1 );

        if( size == ITEM_LIST )
        {
            const QRect thumbRect( r.left() + 4, r.center().y() - 15, 32, 32 );
            drawThumbnail( painter, thumbRect, path, b_dir, isAudioFile( name ), b_started, 2, iconPath );
            const QRect textRect( r.left() + 46, r.top(), r.width() - 50, r.height() );
            painter->drawText( textRect, Qt::AlignVCenter | Qt::AlignLeft,
                               fm.elidedText( name, Qt::ElideMiddle, textRect.width() ) );
        }
        else
        {
            const QSize thumbSize = size == ITEM_LARGE ? QSize( 200, 112 ) : QSize( 96, 54 );
            const QRect thumbRect( r.center().x() - thumbSize.width() / 2, r.top() + 6,
                                   thumbSize.width(), thumbSize.height() );
            drawThumbnail( painter, thumbRect, path, b_dir, isAudioFile( name ), b_started, 6, iconPath );
            const QRect textRect( r.left() + 6, thumbRect.bottom() + 8, r.width() - 12, fm.height() );
            painter->drawText( textRect, Qt::AlignLeft | Qt::AlignVCenter,
                               fm.elidedText( name, Qt::ElideMiddle, textRect.width() ) );
        }
        painter->restore();
    }

    QSize sizeHint( const QStyleOptionViewItem &option, const QModelIndex & ) const Q_DECL_OVERRIDE
    {
        switch( view->property( "itemSize" ).toInt() )
        {
            case ITEM_LARGE: return QSize( 212, 152 );
            case ITEM_SMALL: return QSize( 108, 92 );
            default:         return QSize( option.rect.width(), 36 );
        }
    }

private:
    intf_thread_t *p_intf;
    QListView *view;
};

} // namespace

/* Figma "Player - Home": rows of recent media and folders */
class LibraryHome : public QScrollArea
{
public:
    LibraryHome( intf_thread_t *_p_intf, std::function<void( const QString & )> _openFolder )
        : p_intf( _p_intf ), openFolder( _openFolder )
    {
        setObjectName( "libraryHome" );
        setFrameShape( QFrame::NoFrame );
        setWidgetResizable( true );
        setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );

        QWidget *content = new QWidget;
        content->setObjectName( "libraryHomeContent" );
        layout = new QVBoxLayout( content );
        layout->setContentsMargins( 0, 10, 0, 10 );
        layout->setSpacing( 12 );
        continueModel = addSection( qtr( "Continue Watching" ) );
        recentModel = addSection( qtr( "Recently Played" ) );
        foldersModel = addSection( qtr( "Frequently Visited Folders" ) );
        layout->addStretch( 1 );
        setWidget( content );
    }

    void refresh()
    {
        continueModel->clear();
        recentModel->clear();
        foldersModel->clear();

        RecentsMRL *recents = RecentsMRL::getInstance( p_intf );
        QHash<QString, int> folderCount;
        QStringList folderOrder;
        foreach( const QString &mrl, recents->recentList() )
        {
            const QUrl url( mrl );
            if( !url.isLocalFile() || !QFileInfo( url.toLocalFile() ).exists() )
                continue;
            const QString path = QDir::cleanPath( url.toLocalFile() );
            if( QFileInfo( path ).isDir() )
                continue;
            ( recents->time( mrl ) > 0 ? continueModel : recentModel )->appendRow( makeItem( mrl, path, false ) );

            const QString folder = QFileInfo( path ).absolutePath();
            if( !folderCount.contains( folder ) )
                folderOrder << folder;
            folderCount[folder]++;
        }
        std::stable_sort( folderOrder.begin(), folderOrder.end(),
                          [&]( const QString &a, const QString &b ) { return folderCount[a] > folderCount[b]; } );
        foreach( const QString &folder, folderOrder )
            foldersModel->appendRow( makeItem( QString(), folder, true ) );

        /* Empty sections are hidden (the page may not be visible yet) */
        for( int i = 0; i < rows.count(); i++ )
        {
            const bool b_empty = rows.at( i )->model()->rowCount() == 0;
            rows.at( i )->setHidden( b_empty );
            titles.at( i )->setHidden( b_empty );
        }
    }

private:
    QStandardItem *makeItem( const QString &mrl, const QString &path, bool b_folder )
    {
        QStandardItem *item = new QStandardItem( QFileInfo( path ).fileName() );
        item->setData( mrl, MRL_ROLE );
        item->setData( path, QFileSystemModel::FilePathRole );
        item->setData( b_folder, FOLDER_ROLE );
        item->setToolTip( QDir::toNativeSeparators( path ) );
        item->setEditable( false );
        return item;
    }

    QStandardItemModel *addSection( const QString &title )
    {
        QLabel *label = new QLabel( title + QString::fromUtf8( "  ›" ) );
        label->setObjectName( "libraryHomeTitle" );
        layout->addWidget( label );
        titles << label;

        QStandardItemModel *model = new QStandardItemModel( this );
        QListView *row = new QListView;
        row->setObjectName( "libraryView" );
        row->setProperty( "itemSize", ITEM_LARGE );
        row->setModel( model );
        row->setItemDelegate( new LibraryItemDelegate( p_intf, row ) );
        row->setViewMode( QListView::IconMode );
        row->setFlow( QListView::LeftToRight );
        row->setWrapping( false );
        row->setMovement( QListView::Static );
        row->setFrameShape( QFrame::NoFrame );
        row->setFixedHeight( 172 );
        row->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        row->setSpacing( 4 );
        row->viewport()->setAttribute( Qt::WA_Hover );
        QObject::connect( row, &QListView::activated, row, [this]( const QModelIndex &index ) {
            if( index.data( FOLDER_ROLE ).toBool() )
                openFolder( index.data( QFileSystemModel::FilePathRole ).toString() );
            else
                Open::openMRL( p_intf, index.data( MRL_ROLE ).toString(), true, true );
        } );
        QObject::connect( Thumbnailer::instance(), &Thumbnailer::thumbnailReady, row->viewport(),
                          static_cast<void (QWidget::*)()>( &QWidget::update ) );
        layout->addWidget( row );
        rows << row;
        return model;
    }

    intf_thread_t *p_intf;
    std::function<void( const QString & )> openFolder;
    QVBoxLayout *layout;
    QList<QLabel *> titles;
    QList<QListView *> rows;
    QStandardItemModel *continueModel;
    QStandardItemModel *recentModel;
    QStandardItemModel *foldersModel;
};

LibrarySidebar::LibrarySidebar( intf_thread_t *_p_intf, QWidget *parent )
              : QFrame( parent ), p_intf( _p_intf ), b_expanded( false ), b_grid( false )
{
    setFixedWidth( COMPACT_WIDTH );

    QHBoxLayout *layout = new QHBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    /* Navigation rail: icons only next to the video, icons + labels when expanded */
    railWidget = new QWidget;
    railWidget->setObjectName( "libraryRail" );
    railWidget->setFixedWidth( RAIL_WIDTH );
    QVBoxLayout *railLayout = new QVBoxLayout( railWidget );
    railLayout->setContentsMargins( 8, 8, 8, 8 );
    railLayout->setSpacing( 14 );
    rail = new QButtonGroup( this );
    addRailButton( SECTION_HOME, ":/redesign/lib-home.svg", qtr( "Home" ) );
    addRailButton( SECTION_VIDEOS, ":/redesign/lib-videos.svg", qtr( "Videos" ) );
    addRailButton( SECTION_MUSIC, ":/redesign/lib-music.svg", qtr( "Music" ) );
    addRailButton( SECTION_NETWORK, ":/redesign/lib-network.svg", qtr( "Network" ) );
    addRailButton( SECTION_PLAYLISTS, ":/redesign/lib-playlists.svg", qtr( "Playlists" ) );
    addRailButton( SECTION_RECENT, ":/redesign/lib-recent.svg", qtr( "Recent" ) );
    addRailButton( SECTION_ADD, ":/redesign/lib-add.svg", qtr( "Add item" ) );
    rail->button( SECTION_PLAYLISTS )->setToolTip( qtr( "Playlists you saved" ) );
    rail->button( SECTION_HOME )->setToolTip( qtr( "Home: recent media and folders" ) );
    rail->button( SECTION_ADD )->setToolTip( qtr( "Browse another folder" ) );
    foreach( QAbstractButton *b, rail->buttons() )
        railLayout->addWidget( b, 0, Qt::AlignHCenter );
    railLayout->addStretch( 1 );
    connect( rail, static_cast<void (QButtonGroup::*)(int)>( &QButtonGroup::buttonClicked ),
             this, &LibrarySidebar::selectSection );
    layout->addWidget( railWidget );

    /* Browser panel */
    QWidget *panel = new QWidget;
    panel->setObjectName( "libraryPanel" );
    QVBoxLayout *panelLayout = new QVBoxLayout( panel );
    panelLayout->setContentsMargins( 20, 20, 20, 0 );
    panelLayout->setSpacing( 17 );

    QHBoxLayout *navLayout = new QHBoxLayout;
    navLayout->setSpacing( 10 );
    backButton = createButton( ":/redesign/arrow-left.svg", qtr( "Back" ) );
    connect( backButton, &QToolButton::clicked, this, &LibrarySidebar::goBack );
    forwardButton = createButton( ":/redesign/arrow-right.svg", qtr( "Forward" ) );
    connect( forwardButton, &QToolButton::clicked, this, &LibrarySidebar::goForward );
    navLayout->addWidget( backButton );
    navLayout->addWidget( forwardButton );
    breadcrumb = new QWidget;
    breadcrumbLayout = new QHBoxLayout( breadcrumb );
    breadcrumbLayout->setContentsMargins( 0, 0, 0, 0 );
    breadcrumbLayout->setSpacing( 6 );
    navLayout->addWidget( breadcrumb, 1 );
    QToolButton *moreButton = createButton( ":/redesign/lib-more.svg", qtr( "More" ) );
    connect( moreButton, &QToolButton::clicked, this, &LibrarySidebar::showMoreMenu );
    navLayout->addWidget( moreButton );
    popoutButton = createButton( ":/redesign/lib-popout.svg", qtr( "Open the full library" ) );
    connect( popoutButton, &QToolButton::clicked, this, [this]() { emit expandRequested( !b_expanded ); } );
    navLayout->addWidget( popoutButton );
    panelLayout->addLayout( navLayout );

    QHBoxLayout *infoLayout = new QHBoxLayout;
    infoLayout->setSpacing( 12 );
    countLabel = new QLabel;
    countLabel->setObjectName( "libraryCount" );
    infoLayout->addWidget( countLabel, 1 );
    QToolButton *sortButton = createButton( ":/redesign/queue-sort.svg", qtr( "Sort" ) );
    connect( sortButton, &QToolButton::clicked, this, &LibrarySidebar::showSortMenu );
    QToolButton *viewButton = createButton( ":/redesign/lib-view.svg", qtr( "List or grid" ) );
    connect( viewButton, &QToolButton::clicked, this, &LibrarySidebar::toggleViewMode );
    QToolButton *searchButton = createButton( ":/redesign/lib-search.svg", qtr( "Search" ) );
    connect( searchButton, &QToolButton::clicked, this, &LibrarySidebar::toggleSearch );
    infoLayout->addWidget( sortButton );
    infoLayout->addWidget( viewButton );
    infoLayout->addWidget( searchButton );
    panelLayout->addLayout( infoLayout );

    searchEdit = new QLineEdit;
    searchEdit->setObjectName( "librarySearch" );
    searchEdit->setPlaceholderText( qtr( "Search" ) );
    searchEdit->setClearButtonEnabled( true );
    searchEdit->hide();
    connect( searchEdit, &QLineEdit::textChanged, this, &LibrarySidebar::setFilter );
    panelLayout->addWidget( searchEdit );

    QFrame *divider = new QFrame;
    divider->setObjectName( "libraryDivider" );
    divider->setFixedHeight( 1 );

    /* Media files and folders only */
    fsModel = new QFileSystemModel( this );
    fsModel->setFilter( QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot );
    fsModel->setNameFilters( QString( EXTENSIONS_MEDIA ).split( ';', QString::SkipEmptyParts ) );
    fsModel->setNameFilterDisables( false );
    fsModel->setReadOnly( true );
    proxy = new LibraryFilterProxy( this );
    proxy->setSourceModel( fsModel );
    proxy->setFilterCaseSensitivity( Qt::CaseInsensitive );
    proxy->setSortCaseSensitivity( Qt::CaseInsensitive );
    proxy->setDynamicSortFilter( true );
    proxy->sort( 0, Qt::AscendingOrder );
    recentModel = new QStandardItemModel( this );
    playlistModel = new QStandardItemModel( this );

    view = new QListView;
    view->setObjectName( "libraryView" );
    view->setProperty( "itemSize", ITEM_LIST );
    view->setFrameShape( QFrame::NoFrame );
    view->setItemDelegate( new LibraryItemDelegate( p_intf, view ) );
    view->setSpacing( 3 );
    view->setUniformItemSizes( true );
    view->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view->setSelectionMode( QAbstractItemView::ExtendedSelection );
    view->setDragEnabled( true );
    view->setDragDropMode( QAbstractItemView::DragOnly );
    view->setContextMenuPolicy( Qt::CustomContextMenu );
    view->setResizeMode( QListView::Adjust );
    view->viewport()->setAttribute( Qt::WA_Hover );
    connect( view, &QListView::activated, this, &LibrarySidebar::activate );
    connect( view, &QListView::customContextMenuRequested, this, &LibrarySidebar::showContextMenu );

    home = new LibraryHome( p_intf, [this]( const QString &folder ) { showFolder( folder ); } );

    pages = new QStackedWidget;
    pages->addWidget( view );
    pages->addWidget( home );

    QVBoxLayout *listLayout = new QVBoxLayout;
    listLayout->setContentsMargins( 0, 0, 0, 0 );
    listLayout->setSpacing( 10 );
    listLayout->addWidget( divider );
    listLayout->addWidget( pages, 1 );
    panelLayout->addLayout( listLayout, 1 );
    layout->addWidget( panel, 1 );

    connect( fsModel, &QFileSystemModel::directoryLoaded, this, &LibrarySidebar::updateCount );
    connect( proxy, &QAbstractItemModel::rowsInserted, this, &LibrarySidebar::updateCount );
    connect( proxy, &QAbstractItemModel::rowsRemoved, this, &LibrarySidebar::updateCount );
    connect( proxy, &QAbstractItemModel::layoutChanged, this, &LibrarySidebar::updateCount );
    /* Highlight the media being played, show thumbnails once generated */
    connect( THEMIM, &MainInputManager::inputChanged, view->viewport(),
             static_cast<void (QWidget::*)()>( &QWidget::update ) );
    connect( Thumbnailer::instance(), &Thumbnailer::thumbnailReady, view->viewport(),
             static_cast<void (QWidget::*)()>( &QWidget::update ) );

    updateHistoryButtons();
    hide();
}

/* Never let the wheel reach the main window, which turns it into volume
 * (the lists pass it on once they reach their top or bottom) */
void LibrarySidebar::wheelEvent( QWheelEvent *event )
{
    event->accept();
}

QToolButton *LibrarySidebar::createButton( const QString &icon, const QString &tooltip, int size )
{
    QToolButton *button = new QToolButton;
    /* Figma shows unavailable actions at 30% opacity */
    QIcon qicon( icon );
    const QPixmap normal = qicon.pixmap( QSize( size, size ) );
    QPixmap disabled( normal.size() );
    disabled.fill( Qt::transparent );
    QPainter painter( &disabled );
    painter.setOpacity( 0.3 );
    painter.drawPixmap( 0, 0, normal );
    painter.end();
    qicon.addPixmap( disabled, QIcon::Disabled );
    button->setIcon( qicon );
    button->setIconSize( QSize( size, size ) );
    button->setFixedSize( QSize( size + 4, size + 4 ) );
    button->setToolTip( tooltip );
    button->setFocusPolicy( Qt::NoFocus );
    button->setAutoRaise( true );
    return button;
}

void LibrarySidebar::addRailButton( int id, const QString &icon, const QString &label )
{
    QToolButton *button = createButton( icon, label );
    button->setObjectName( "libraryRailButton" );
    button->setText( label );
    button->setFixedSize( 30, 30 );
    /* the section shown is orange; network and add open dialogs */
    if( id != SECTION_NETWORK && id != SECTION_ADD )
    {
        button->setCheckable( true );
        button->setIcon( Redesign::activeIcon( QIcon( icon ), QSize( 24, 24 ) ) );
    }
    rail->addButton( button, id );
}

void LibrarySidebar::markSection( int id )
{
    rail->setExclusive( false );
    foreach( QAbstractButton *b, rail->buttons() )
        b->setChecked( rail->id( b ) == id );
    rail->setExclusive( true );
}

/* The expanded library takes the whole window, the video becomes a mini player */
void LibrarySidebar::setExpanded( bool b_expand )
{
    b_expanded = b_expand;
    if( b_expanded )
    {
        setMinimumWidth( 0 );
        setMaximumWidth( QWIDGETSIZE_MAX );
    }
    else
        setFixedWidth( COMPACT_WIDTH );

    railWidget->setFixedWidth( b_expanded ? RAIL_EXPANDED_WIDTH : RAIL_WIDTH );
    foreach( QAbstractButton *b, rail->buttons() )
    {
        QToolButton *button = static_cast<QToolButton *>( b );
        button->setToolButtonStyle( b_expanded ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly );
        button->setFixedSize( b_expanded ? QSize( RAIL_EXPANDED_WIDTH - 32, 32 ) : QSize( 30, 30 ) );
    }
    railWidget->layout()->setAlignment( Qt::AlignTop );
    foreach( QAbstractButton *b, rail->buttons() )
        railWidget->layout()->setAlignment( b, b_expanded ? Qt::AlignLeft : Qt::AlignHCenter );

    popoutButton->setToolTip( b_expanded ? qtr( "Back to the player" ) : qtr( "Open the full library" ) );
    setGridMode( b_expanded );
}

void LibrarySidebar::selectSection( int id )
{
    switch( id )
    {
    case SECTION_HOME:
        if( b_expanded )
            showHome();
        else
            showCurrentMediaFolder();
        break;
    case SECTION_VIDEOS:
        showFolder( QStandardPaths::writableLocation( QStandardPaths::MoviesLocation ) );
        break;
    case SECTION_MUSIC:
        showFolder( QStandardPaths::writableLocation( QStandardPaths::MusicLocation ) );
        break;
    case SECTION_NETWORK:
        THEDP->openNetDialog();
        break;
    case SECTION_PLAYLISTS:
        showPlaylists();
        break;
    case SECTION_RECENT:
        showRecent();
        break;
    case SECTION_ADD:
    {
        const QString dir = QFileDialog::getExistingDirectory( this, qtr( "Browse a folder" ), currentPath );
        if( !dir.isEmpty() )
            showFolder( dir );
        break;
    }
    default:
        break;
    }
}

QString LibrarySidebar::location() const
{
    return currentPage.isEmpty() ? currentPath : currentPage;
}

void LibrarySidebar::pushHistory()
{
    const QString here = location();
    if( !here.isEmpty() )
        backHistory << here;
    forwardHistory.clear();
}

void LibrarySidebar::openLocation( const QString &where )
{
    if( where == PAGE_RECENT )
        showRecent();
    else if( where == PAGE_HOME )
        showHome();
    else if( where == PAGE_PLAYLISTS )
        showPlaylists();
    else if( where.startsWith( PAGE_PLAYLIST ) )
        showPlaylist( where.mid( PAGE_PLAYLIST.length() ) );
    else
        showFolder( where, false );
}

void LibrarySidebar::showCurrentMediaFolder()
{
    const QString media = currentMediaPath( p_intf );
    showFolder( media.isEmpty() ? ( currentPath.isEmpty() ? QDir::homePath() : currentPath )
                                : QFileInfo( media ).absolutePath() );
    /* next to the video, Home is the folder of the media */
    if( rail->checkedId() < 0 && !b_expanded )
        markSection( SECTION_HOME );
    if( !media.isEmpty() )
    {
        const QModelIndex index = proxy->mapFromSource( fsModel->index( media ) );
        if( index.isValid() )
        {
            view->setCurrentIndex( index );
            view->scrollTo( index );
        }
    }
}

void LibrarySidebar::showFolder( const QString &path, bool b_record )
{
    const QString folder = QDir::cleanPath( path );
    if( folder.isEmpty() || !QFileInfo( folder ).isDir() )
        return;
    if( b_record && location() != folder )
        pushHistory();

    currentPage.clear();
    currentPath = folder;
    if( view->model() != proxy )
        view->setModel( proxy );
    fsModel->setRootPath( folder );
    proxy->setFolder( folder );
    view->setRootIndex( proxy->mapFromSource( fsModel->index( folder ) ) );
    view->setDragDropMode( QAbstractItemView::DragOnly );
    pages->setCurrentWidget( view );

    /* the Videos and Music sections are the folders of the same name */
    auto inside = [&folder]( QStandardPaths::StandardLocation location ) {
        const QString base = QDir::cleanPath( QStandardPaths::writableLocation( location ) );
        return !base.isEmpty() && ( folder.compare( base, Qt::CaseInsensitive ) == 0 ||
                                    folder.startsWith( base + "/", Qt::CaseInsensitive ) );
    };
    markSection( inside( QStandardPaths::MoviesLocation ) ? SECTION_VIDEOS
               : inside( QStandardPaths::MusicLocation ) ? SECTION_MUSIC : -1 );

    updateBreadcrumb();
    updateHistoryButtons();
    updateCount();
}

void LibrarySidebar::showRecent()
{
    if( location() != PAGE_RECENT )
        pushHistory();
    currentPage = PAGE_RECENT;
    markSection( SECTION_RECENT );

    recentModel->clear();
    foreach( const QString &mrl, RecentsMRL::getInstance( p_intf )->recentList() )
    {
        const QUrl url( mrl );
        const QString name = url.isLocalFile() ? QFileInfo( url.toLocalFile() ).fileName() : mrl;
        QStandardItem *item = new QStandardItem( name );
        item->setData( mrl, MRL_ROLE );
        item->setData( url.isLocalFile() ? url.toLocalFile() : QString(), QFileSystemModel::FilePathRole );
        item->setToolTip( url.isLocalFile() ? QDir::toNativeSeparators( url.toLocalFile() ) : mrl );
        item->setEditable( false );
        recentModel->appendRow( item );
    }
    view->setModel( recentModel );
    view->setRootIndex( QModelIndex() );
    pages->setCurrentWidget( view );

    updateBreadcrumb();
    updateHistoryButtons();
    updateCount();
}

void LibrarySidebar::showHome()
{
    if( location() != PAGE_HOME )
        pushHistory();
    currentPage = PAGE_HOME;
    markSection( SECTION_HOME );
    home->refresh();
    pages->setCurrentWidget( home );

    updateBreadcrumb();
    updateHistoryButtons();
    updateCount();
}

void LibrarySidebar::goBack()
{
    if( backHistory.isEmpty() )
        return;
    forwardHistory.prepend( location() );
    const QString target = backHistory.takeLast();
    const QStringList forward = forwardHistory;
    currentPage = target; /* avoid recording the jump */
    if( !isPage( target ) )
        currentPage.clear(), currentPath = target;
    openLocation( target );
    forwardHistory = forward;
    updateHistoryButtons();
}

void LibrarySidebar::goForward()
{
    if( forwardHistory.isEmpty() )
        return;
    const QString target = forwardHistory.takeFirst();
    const QStringList forward = forwardHistory;
    backHistory << location();
    currentPage = target;
    if( !isPage( target ) )
        currentPage.clear(), currentPath = target;
    openLocation( target );
    forwardHistory = forward;
    updateHistoryButtons();
}

void LibrarySidebar::updateHistoryButtons()
{
    backButton->setEnabled( !backHistory.isEmpty() );
    forwardButton->setEnabled( !forwardHistory.isEmpty() );
}

/* "… ▸ parent ▸ folder": the last levels, "…" goes one level further up */
void LibrarySidebar::updateBreadcrumb()
{
    QLayoutItem *item;
    while( ( item = breadcrumbLayout->takeAt( 0 ) ) )
    {
        delete item->widget();
        delete item;
    }

    auto addSeparator = [this]() {
        QLabel *sep = new QLabel( QString::fromUtf8( "▶" ) );
        sep->setObjectName( "breadcrumbSeparator" );
        breadcrumbLayout->addWidget( sep );
    };
    auto addCrumb = [this]( const QString &text, const QString &target ) {
        QToolButton *crumb = new QToolButton;
        crumb->setObjectName( "breadcrumb" );
        /* long names end with "…" instead of losing their start */
        crumb->ensurePolished();
        crumb->setText( crumb->fontMetrics().elidedText( text, Qt::ElideRight, b_expanded ? 260 : 130 ) );
        crumb->setToolTip( target.isEmpty() ? text : QDir::toNativeSeparators( target ) );
        crumb->setFocusPolicy( Qt::NoFocus );
        crumb->setAutoRaise( true );
        if( target.isEmpty() || target == location() )
            crumb->setEnabled( false );
        else
            connect( crumb, &QToolButton::clicked, this, [this, target]() {
                if( isPage( target ) )
                    openLocation( target );
                else
                    showFolder( target );
            } );
        breadcrumbLayout->addWidget( crumb );
    };

    if( currentPage == PAGE_RECENT )
        addCrumb( qtr( "Recent" ), QString() );
    else if( currentPage == PAGE_HOME )
        addCrumb( qtr( "Home" ), QString() );
    else if( currentPage == PAGE_PLAYLISTS )
        addCrumb( qtr( "Playlists" ), QString() );
    else if( currentPage.startsWith( PAGE_PLAYLIST ) )
    {
        addCrumb( qtr( "Playlists" ), PAGE_PLAYLISTS );
        addSeparator();
        addCrumb( Playlists::read( currentPage.mid( PAGE_PLAYLIST.length() ) ).title, QString() );
    }
    else
    {
        QStringList levels;
        for( QDir dir( currentPath ); ; )
        {
            levels.prepend( dir.absolutePath() );
            if( !dir.cdUp() )
                break;
        }
        const int shown = b_expanded ? 4 : 2;
        const int first = qMax( 0, levels.count() - shown );
        if( first > 0 )
        {
            addCrumb( "...", levels.at( first - 1 ) );
            addSeparator();
        }
        for( int i = first; i < levels.count(); i++ )
        {
            QString name = QFileInfo( levels.at( i ) ).fileName();
            if( name.isEmpty() )
                name = QDir::toNativeSeparators( levels.at( i ) ); /* drive or root */
            addCrumb( name, levels.at( i ) );
            if( i + 1 < levels.count() )
                addSeparator();
        }
    }
    breadcrumbLayout->addStretch( 1 );
}

void LibrarySidebar::updateCount()
{
    if( currentPage == PAGE_HOME )
    {
        countLabel->clear();
        return;
    }
    const int count = view->model() ? view->model()->rowCount( view->rootIndex() ) : 0;
    if( currentPage == PAGE_PLAYLISTS )
        countLabel->setText( count - 1 == 1 ? qtr( "1 playlist" ) : qtr( "%1 playlists" ).arg( count - 1 ) );
    else
        countLabel->setText( count == 1 ? qtr( "1 item" ) : qtr( "%1 items" ).arg( count ) );
}

QString LibrarySidebar::pathFor( const QModelIndex &index ) const
{
    return index.data( QFileSystemModel::FilePathRole ).toString();
}

QString LibrarySidebar::urlFor( const QModelIndex &index ) const
{
    const QString mrl = index.data( MRL_ROLE ).toString();
    return !mrl.isEmpty() ? mrl : QUrl::fromLocalFile( pathFor( index ) ).toString();
}

void LibrarySidebar::enqueue( const QModelIndex &index, bool b_play )
{
    const QString playlist = index.data( PLAYLIST_ROLE ).toString();
    if( !playlist.isEmpty() )
    {
        const QStringList tracks = Playlists::read( playlist ).tracks;
        for( int i = 0; i < tracks.count(); i++ )
            Open::openMRL( p_intf, tracks.at( i ), b_play && i == 0, true );
        return;
    }
    Open::openMRL( p_intf, urlFor( index ), b_play, true );
}

/* Plays the first one, the others go to the queue after it */
void LibrarySidebar::playAll( const QStringList &urls )
{
    for( int i = 0; i < urls.count(); i++ )
        Open::openMRL( p_intf, urls.at( i ), i == 0, true );
}

/* "Add to playlist" > [New playlist...] [each saved playlist] */
void LibrarySidebar::addPlaylistMenu( QMenu *menu, const QStringList &urls )
{
    QMenu *submenu = menu->addMenu( QIcon( ":/redesign/lib-playlists.svg" ), qtr( "Add to playlist" ) );
    submenu->addAction( QIcon( ":/redesign/lib-add-small.svg" ), qtr( "New playlist..." ), this, [this, urls]() {
        if( !NewPlaylistDialog::create( p_intf, window(), urls ).isEmpty() )
            refreshPlaylistPage();
    } );
    const QStringList playlists = Playlists::list( p_intf );
    if( !playlists.isEmpty() )
        submenu->addSeparator();
    foreach( const QString &file, playlists )
        submenu->addAction( Playlists::read( file ).title, this, [this, file, urls]() {
            Playlists::append( file, urls );
            refreshPlaylistPage();
        } );
}

/* The playlist pages show what is on disk: reload them without a history step */
void LibrarySidebar::refreshPlaylistPage()
{
    const QString page = currentPage;
    if( page != PAGE_PLAYLISTS && !page.startsWith( PAGE_PLAYLIST ) )
        return;
    const QStringList back = backHistory, forward = forwardHistory;
    openLocation( page );
    backHistory = back;
    forwardHistory = forward;
    updateHistoryButtons();
}

/* Figma "Playlists": [+ New playlist] then one card per saved playlist */
void LibrarySidebar::showPlaylists()
{
    if( location() != PAGE_PLAYLISTS )
        pushHistory();
    currentPage = PAGE_PLAYLISTS;
    markSection( SECTION_PLAYLISTS );

    playlistModel->clear();
    QStandardItem *create = new QStandardItem( qtr( "New playlist" ) );
    create->setData( true, NEW_ROLE );
    create->setData( ":/redesign/lib-add-small.svg", ICON_ROLE );
    create->setToolTip( qtr( "Create a playlist" ) );
    create->setEditable( false );
    playlistModel->appendRow( create );
    foreach( const QString &file, Playlists::list( p_intf ) )
    {
        const Playlists::Info info = Playlists::read( file );
        QString thumb = info.image;
        if( thumb.isEmpty() || !QFileInfo( thumb ).exists() )
        {
            thumb.clear();
            foreach( const QString &track, info.tracks )
                if( QUrl( track ).isLocalFile() && QFileInfo( QUrl( track ).toLocalFile() ).isFile() )
                {
                    thumb = QUrl( track ).toLocalFile();
                    break;
                }
        }
        QStandardItem *item = new QStandardItem( info.title );
        item->setData( file, PLAYLIST_ROLE );
        item->setData( thumb, QFileSystemModel::FilePathRole );
        item->setData( ":/redesign/lib-playlists.svg", ICON_ROLE );
        item->setToolTip( info.tracks.count() == 1 ? qtr( "%1 (1 item)" ).arg( info.title )
                                                   : qtr( "%1 (%2 items)" ).arg( info.title ).arg( info.tracks.count() ) );
        item->setEditable( false );
        playlistModel->appendRow( item );
    }
    view->setModel( playlistModel );
    view->setRootIndex( QModelIndex() );
    pages->setCurrentWidget( view );

    updateBreadcrumb();
    updateHistoryButtons();
    updateCount();
}

/* The tracks of one playlist */
void LibrarySidebar::showPlaylist( const QString &file )
{
    const QString page = PAGE_PLAYLIST + file;
    if( location() != page )
        pushHistory();
    currentPage = page;
    markSection( SECTION_PLAYLISTS );

    recentModel->clear();
    const QStringList tracks = Playlists::read( file ).tracks;
    for( int i = 0; i < tracks.count(); i++ )
    {
        const QUrl url( tracks.at( i ) );
        const QString name = url.isLocalFile() ? QFileInfo( url.toLocalFile() ).fileName() : tracks.at( i );
        QStandardItem *item = new QStandardItem( name );
        item->setData( tracks.at( i ), MRL_ROLE );
        item->setData( url.isLocalFile() ? url.toLocalFile() : QString(), QFileSystemModel::FilePathRole );
        item->setData( i, TRACK_ROLE );
        item->setToolTip( url.isLocalFile() ? QDir::toNativeSeparators( url.toLocalFile() ) : tracks.at( i ) );
        item->setEditable( false );
        recentModel->appendRow( item );
    }
    view->setModel( recentModel );
    view->setRootIndex( QModelIndex() );
    pages->setCurrentWidget( view );

    updateBreadcrumb();
    updateHistoryButtons();
    updateCount();
}

void LibrarySidebar::activate( const QModelIndex &index )
{
    if( index.data( NEW_ROLE ).toBool() )
    {
        if( !NewPlaylistDialog::create( p_intf, window() ).isEmpty() )
            refreshPlaylistPage();
        return;
    }
    const QString playlist = index.data( PLAYLIST_ROLE ).toString();
    if( !playlist.isEmpty() )
    {
        showPlaylist( playlist );
        return;
    }
    const QString path = pathFor( index );
    if( index.data( MRL_ROLE ).toString().isEmpty() && QFileInfo( path ).isDir() )
        showFolder( path );
    else
        enqueue( index, true );
    view->viewport()->update();
}

void LibrarySidebar::showContextMenu( const QPoint &pos )
{
    const QModelIndex index = view->indexAt( pos );
    QMenu menu( this );
    const bool b_folderPage = currentPage.isEmpty();
    const QString playlistFile = currentPage.startsWith( PAGE_PLAYLIST )
                               ? currentPage.mid( PAGE_PLAYLIST.length() ) : QString();
    if( currentPage == PAGE_PLAYLISTS )
    {
        if( index.isValid() && !index.data( NEW_ROLE ).toBool() )
        {
            const QString file = index.data( PLAYLIST_ROLE ).toString();
            menu.addAction( qtr( "Play" ), this, [=]() { enqueue( index, true ); } );
            menu.addAction( qtr( "Add to the queue" ), this, [=]() { enqueue( index, false ); } );
            menu.addAction( qtr( "Open" ), this, [=]() { showPlaylist( file ); } );
            menu.addSeparator();
            menu.addAction( qtr( "Delete the playlist" ), this, [=]() {
                if( QMessageBox::question( window(), qtr( "Delete the playlist" ),
                        qtr( "Delete the playlist \"%1\"? The media files are kept." )
                            .arg( Playlists::read( file ).title ) ) == QMessageBox::Yes )
                {
                    Playlists::remove( file );
                    refreshPlaylistPage();
                }
            } );
        }
        else
            menu.addAction( qtr( "New playlist..." ), this, [this]() {
                if( !NewPlaylistDialog::create( p_intf, window() ).isEmpty() )
                    refreshPlaylistPage();
            } );
        menu.exec( view->viewport()->mapToGlobal( pos ) );
        return;
    }
    if( index.isValid() )
    {
        const QModelIndexList selection = view->selectionModel()->selectedIndexes();
        /* media only: folders can not go in a playlist */
        QStringList urls;
        foreach( const QModelIndex &i, selection.isEmpty() ? QModelIndexList() << index : selection )
            if( !i.data( FOLDER_ROLE ).toBool() && !QFileInfo( pathFor( i ) ).isDir() )
                urls << urlFor( i );
        menu.addAction( qtr( "Play" ), this, [=]() { enqueue( index, true ); } );
        menu.addAction( qtr( "Add to the queue" ), this, [=]() {
            foreach( const QModelIndex &i, selection.isEmpty() ? QModelIndexList() << index : selection )
                enqueue( i, false );
        } );
        if( b_folderPage && !QFileInfo( pathFor( index ) ).isDir() )
        {
            /* Binge mode: this file and everything after it in the folder */
            menu.addAction( qtr( "Play from here" ), this, [=]() {
                const QModelIndex parent = index.parent();
                for( int row = index.row(); row < proxy->rowCount( parent ); row++ )
                {
                    const QModelIndex i = proxy->index( row, 0, parent );
                    if( !QFileInfo( pathFor( i ) ).isDir() )
                        enqueue( i, row == index.row() );
                }
            } );
        }
        if( !playlistFile.isEmpty() )
        {
            menu.addAction( qtr( "Play from here" ), this, [=]() {
                const QStringList tracks = Playlists::read( playlistFile ).tracks;
                playAll( tracks.mid( index.data( TRACK_ROLE ).toInt() ) );
            } );
            menu.addAction( qtr( "Remove from the playlist" ), this, [=]() {
                QList<int> rows;
                foreach( const QModelIndex &i, selection.isEmpty() ? QModelIndexList() << index : selection )
                    rows << i.data( TRACK_ROLE ).toInt();
                std::sort( rows.begin(), rows.end(), std::greater<int>() );
                foreach( int row, rows )
                    Playlists::removeTrack( playlistFile, row );
                refreshPlaylistPage();
            } );
        }
        else if( !urls.isEmpty() )
            addPlaylistMenu( &menu, urls );
        menu.addSeparator();
    }
    if( b_folderPage )
        menu.addAction( qtr( "Open the folder in the file manager" ), this, [this]() {
            QDesktopServices::openUrl( QUrl::fromLocalFile( currentPath ) );
        } );
    if( !menu.isEmpty() )
        menu.exec( view->viewport()->mapToGlobal( pos ) );
}

void LibrarySidebar::showMoreMenu()
{
    QMenu menu( this );
    if( currentPage == PAGE_PLAYLISTS )
        menu.addAction( qtr( "New playlist..." ), this, [this]() {
            if( !NewPlaylistDialog::create( p_intf, window() ).isEmpty() )
                refreshPlaylistPage();
        } );
    else if( currentPage.startsWith( PAGE_PLAYLIST ) )
    {
        const QString file = currentPage.mid( PAGE_PLAYLIST.length() );
        menu.addAction( qtr( "Play the playlist" ), this, [=]() { playAll( Playlists::read( file ).tracks ); } );
        menu.addAction( qtr( "Add files..." ), this, [=]() {
            const QList<QUrl> urls = QFileDialog::getOpenFileUrls( window(), qtr( "Add files" ) );
            QStringList list;
            foreach( const QUrl &url, urls )
                list << url.toString( QUrl::FullyEncoded );
            if( !list.isEmpty() )
            {
                Playlists::append( file, list );
                refreshPlaylistPage();
            }
        } );
        menu.addSeparator();
    }
    menu.addAction( qtr( "Browse another folder..." ), this, [this]() { selectSection( SECTION_ADD ); } );
    if( currentPage.isEmpty() )
    {
        menu.addAction( qtr( "Add this folder to the queue" ), this, [this]() {
            Open::openMRL( p_intf, QUrl::fromLocalFile( currentPath ).toString(), false, true );
        } );
        menu.addAction( qtr( "Open the folder in the file manager" ), this, [this]() {
            QDesktopServices::openUrl( QUrl::fromLocalFile( currentPath ) );
        } );
    }
    QWidget *button = qobject_cast<QWidget *>( sender() );
    menu.exec( button ? button->mapToGlobal( QPoint( 0, button->height() ) ) : QCursor::pos() );
}

void LibrarySidebar::showSortMenu()
{
    if( !currentPage.isEmpty() )
        return;
    QMenu menu( this );
    menu.addAction( qtr( "Name (A to Z)" ), this, [this]() { proxy->sort( 0, Qt::AscendingOrder ); } );
    menu.addAction( qtr( "Name (Z to A)" ), this, [this]() { proxy->sort( 0, Qt::DescendingOrder ); } );
    menu.addAction( qtr( "Newest first" ), this, [this]() { proxy->sort( 3, Qt::DescendingOrder ); } );
    menu.addAction( qtr( "Largest first" ), this, [this]() { proxy->sort( 1, Qt::DescendingOrder ); } );
    QWidget *button = qobject_cast<QWidget *>( sender() );
    menu.exec( button ? button->mapToGlobal( QPoint( 0, button->height() ) ) : QCursor::pos() );
}

void LibrarySidebar::setGridMode( bool b_enable )
{
    b_grid = b_enable;
    view->setProperty( "itemSize", !b_grid ? ITEM_LIST : b_expanded ? ITEM_LARGE : ITEM_SMALL );
    view->setViewMode( b_grid ? QListView::IconMode : QListView::ListMode );
    view->setFlow( b_grid ? QListView::LeftToRight : QListView::TopToBottom );
    view->setWrapping( b_grid );
    view->setMovement( QListView::Static );
    view->setResizeMode( QListView::Adjust );
    view->setSpacing( b_grid ? 4 : 3 );
    view->setDragDropMode( QAbstractItemView::DragOnly );
    view->doItemsLayout();
}

void LibrarySidebar::toggleViewMode()
{
    setGridMode( !b_grid );
}

void LibrarySidebar::toggleSearch()
{
    searchEdit->setVisible( !searchEdit->isVisible() );
    if( searchEdit->isVisible() )
        searchEdit->setFocus();
    else
        searchEdit->clear();
}

void LibrarySidebar::setFilter( const QString &text )
{
    proxy->setFilterFixedString( text );
    if( currentPage.isEmpty() )
        view->setRootIndex( proxy->mapFromSource( fsModel->index( currentPath ) ) );
    updateCount();
}
