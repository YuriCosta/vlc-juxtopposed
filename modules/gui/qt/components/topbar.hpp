/*****************************************************************************
 * topbar.hpp : top bar of the redesigned main window
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

#ifndef VLC_QT_TOPBAR_HPP_
#define VLC_QT_TOPBAR_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QFrame>

class QLabel;
class QMenu;
class QMenuBar;
class QToolButton;
class MainInterface;

/* Replaces both the menubar and the native title bar:
 * [cone] [… menu] [sidebar] | [folder]     title     [_] [□] [⛶] [x] */
class TopBar : public QFrame
{
    Q_OBJECT

public:
    TopBar( intf_thread_t *, MainInterface *, QMenuBar *menus );

    /* Window buttons are only shown when the native title bar is hidden */
    void setWindowButtonsVisible( bool );
    /* Whether a point (in TopBar coordinates) should drag the window */
    bool isCaptionArea( const QPoint & ) const;

public slots:
    void setTitle( const QString & );
    void updateMaximizeButton();

private slots:
    void updateFolder();
    void showMainMenu();

private:
    QToolButton *createButton( const QString &icon, const QString &tooltip );

    intf_thread_t *p_intf;
    MainInterface *mi;

    QMenu       *mainMenu;
    QLabel      *titleLabel;
    QToolButton *folderButton;
    QWidget     *windowButtons;
    QToolButton *maximizeButton;
};

#endif
