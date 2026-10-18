/*****************************************************************************
 * library_sidebar.hpp : library of the redesigned interface
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

#ifndef VLC_QT_LIBRARY_SIDEBAR_HPP_
#define VLC_QT_LIBRARY_SIDEBAR_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QFrame>
#include <QStringList>

class QButtonGroup;
class QFileSystemModel;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QModelIndex;
class QStackedWidget;
class QStandardItemModel;
class QToolButton;
class LibraryFilterProxy;
class LibraryHome;

/* Figma "sidebar" (compact, next to the video) and "Player - library" (expanded,
 * the whole window with a mini player). VLC 3 has no media library: sections
 * map to folders, the recent media and the resume positions. */
class LibrarySidebar : public QFrame
{
    Q_OBJECT

public:
    LibrarySidebar( intf_thread_t *, QWidget *parent );

    bool isExpanded() const { return b_expanded; }

public slots:
    void setExpanded( bool );
    void showFolder( const QString &path, bool b_record = true );
    void showCurrentMediaFolder();
    void showRecent();
    void showHome();
    void showPlaylists();
    void showPlaylist( const QString &path );

signals:
    void expandRequested( bool );

protected:
    void wheelEvent( QWheelEvent * ) Q_DECL_OVERRIDE;

private slots:
    void goBack();
    void goForward();
    void activate( const QModelIndex & );
    void updateCount();
    void showSortMenu();
    void showMoreMenu();
    void showContextMenu( const QPoint & );
    void toggleViewMode();
    void toggleSearch();
    void setFilter( const QString & );

private:
    QToolButton *createButton( const QString &icon, const QString &tooltip, int size = 24 );
    void addRailButton( int id, const QString &icon, const QString &label );
    void selectSection( int id );
    void markSection( int id );
    void setGridMode( bool );
    void updateBreadcrumb();
    void updateHistoryButtons();
    void pushHistory();
    void openLocation( const QString & );
    QString location() const;
    QString pathFor( const QModelIndex & ) const;
    void enqueue( const QModelIndex &, bool b_play );
    QString urlFor( const QModelIndex & ) const;
    void addPlaylistMenu( QMenu *, const QStringList &urls );
    void playAll( const QStringList &urls );
    void refreshPlaylistPage();

    intf_thread_t *p_intf;

    QWidget            *railWidget;
    QButtonGroup       *rail;
    QStackedWidget     *pages;
    LibraryHome        *home;
    QFileSystemModel   *fsModel;
    LibraryFilterProxy *proxy;
    QStandardItemModel *recentModel;
    QStandardItemModel *playlistModel;
    QListView          *view;

    QToolButton *backButton;
    QToolButton *forwardButton;
    QToolButton *popoutButton;
    QWidget     *breadcrumb;
    QHBoxLayout *breadcrumbLayout;
    QLabel      *countLabel;
    QLineEdit   *searchEdit;

    bool         b_expanded;
    bool         b_grid;
    /* "" = folder in currentPath, "recent:" / "home:" / "playlists:" /
     * "playlist:<file>" = special pages */
    QString      currentPage;
    QString      currentPath;
    QStringList  backHistory;
    QStringList  forwardHistory;
};

#endif
