/*****************************************************************************
 * topbar.cpp : top bar of the redesigned main window
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

#include "components/topbar.hpp"
#include "main_interface.hpp"
#include "input_manager.hpp"
#include "actions_manager.hpp"
#include "util/imagehelper.hpp"

#include <vlc_input.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <QUrl>

/* Figma: 40px high bar, 300px wide side groups so that the title is centered */
#define TOPBAR_HEIGHT     40
#define TOPBAR_SIDE_WIDTH 300

TopBar::TopBar( intf_thread_t *_p_intf, MainInterface *_mi, QMenuBar *menus )
       : QFrame( _mi ), p_intf( _p_intf ), mi( _mi )
{
    setFixedHeight( TOPBAR_HEIGHT );

    QHBoxLayout *layout = new QHBoxLayout( this );
    layout->setContentsMargins( 6, 0, 6, 0 );
    layout->setSpacing( 0 );

    /* Left: cone, main menu, sidebar, current folder */
    QWidget *left = new QWidget;
    left->setFixedWidth( TOPBAR_SIDE_WIDTH );
    QHBoxLayout *leftLayout = new QHBoxLayout( left );
    leftLayout->setContentsMargins( 0, 0, 0, 0 );
    leftLayout->setSpacing( 7 );

    QLabel *cone = new QLabel;
    cone->setPixmap( ImageHelper::loadSvgToPixmap( ":/redesign/cone.svg", 24, 24 ) );
    cone->setContentsMargins( 7, 2, 7, 2 );
    leftLayout->addWidget( cone );

    /* The menubar is hidden: its menus live behind the "…" button */
    mainMenu = new QMenu( this );
    foreach( QAction *action, menus->actions() )
        if( action->menu() )
            mainMenu->addMenu( action->menu() );

    QToolButton *moreButton = createButton( ":/redesign/more.svg", qtr( "Menu" ) );
    connect( moreButton, &QToolButton::clicked, this, &TopBar::showMainMenu );
    leftLayout->addWidget( moreButton );

    QToolButton *sidebarButton = createButton( ":/redesign/sidebar.svg", qtr( "Show or hide the library" ) );
    connect( sidebarButton, &QToolButton::clicked, mi, &MainInterface::toggleLibrarySidebar );
    leftLayout->addWidget( sidebarButton );

    QFrame *divider = new QFrame;
    divider->setObjectName( "topBarDivider" );
    divider->setFixedSize( 1, 18 );
    leftLayout->addWidget( divider );

    /* Name of the folder of the current media, the shortcut to the library */
    folderButton = createButton( ":/redesign/folder.svg", qtr( "Show this folder" ) );
    folderButton->setObjectName( "topBarFolder" );
    folderButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    /* Unlike icon buttons, it grows with the folder name */
    folderButton->setMinimumSize( 0, 28 );
    folderButton->setMaximumSize( TOPBAR_SIDE_WIDTH - 120, 28 );
    connect( folderButton, &QToolButton::clicked, mi, &MainInterface::showLibraryForCurrentMedia );
    leftLayout->addWidget( folderButton );
    leftLayout->addStretch( 1 );
    layout->addWidget( left );

    /* Center: media title */
    titleLabel = new QLabel;
    titleLabel->setObjectName( "topBarTitle" );
    titleLabel->setAlignment( Qt::AlignCenter );
    titleLabel->setMinimumWidth( 1 );
    layout->addWidget( titleLabel, 1 );

    /* Right: window buttons */
    windowButtons = new QWidget;
    windowButtons->setFixedWidth( TOPBAR_SIDE_WIDTH );
    QHBoxLayout *rightLayout = new QHBoxLayout( windowButtons );
    rightLayout->setContentsMargins( 0, 0, 0, 0 );
    rightLayout->setSpacing( 0 );
    rightLayout->addStretch( 1 );

    QToolButton *minimizeButton = createButton( ":/redesign/minimize.svg", qtr( "Minimize" ) );
    connect( minimizeButton, &QToolButton::clicked, mi, &QWidget::showMinimized );
    rightLayout->addWidget( minimizeButton );

    maximizeButton = createButton( ":/redesign/maximize.svg", qtr( "Maximize" ) );
    connect( maximizeButton, &QToolButton::clicked, this, [this]() {
        if( mi->isMaximized() )
            mi->showNormal();
        else
            mi->showMaximized();
    } );
    rightLayout->addWidget( maximizeButton );

    QToolButton *fullscreenButton = createButton( ":/toolbar/fullscreen.svg", qtr( "Fullscreen" ) );
    connect( fullscreenButton, &QToolButton::clicked, this, [this]() {
        ActionsManager::getInstance( p_intf )->doAction( FULLSCREEN_ACTION );
    } );
    rightLayout->addWidget( fullscreenButton );

    QToolButton *closeButton = createButton( ":/redesign/close.svg", qtr( "Close" ) );
    closeButton->setObjectName( "topBarClose" );
    connect( closeButton, &QToolButton::clicked, mi, &QWidget::close );
    rightLayout->addWidget( closeButton );

    layout->addWidget( windowButtons );

    setTitle( QString() );
    updateFolder();
    connect( THEMIM, &MainInputManager::inputChanged, this, &TopBar::updateFolder );
}

QToolButton *TopBar::createButton( const QString &icon, const QString &tooltip )
{
    QToolButton *button = new QToolButton;
    button->setIcon( QIcon( icon ) );
    button->setIconSize( QSize( 24, 24 ) );
    button->setFixedSize( QSize( 36, 28 ) );
    button->setToolTip( tooltip );
    button->setFocusPolicy( Qt::NoFocus );
    button->setAutoRaise( true );
    return button;
}

void TopBar::setWindowButtonsVisible( bool b_visible )
{
    /* Keep the width so that the title stays centered */
    foreach( QToolButton *button, windowButtons->findChildren<QToolButton *>() )
        button->setVisible( b_visible );
}

bool TopBar::isCaptionArea( const QPoint &pos ) const
{
    if( !rect().contains( pos ) )
        return false;
    for( QWidget *w = childAt( pos ); w && w != this; w = w->parentWidget() )
        if( qobject_cast<QAbstractButton *>( w ) )
            return false;
    return true;
}

void TopBar::setTitle( const QString &title )
{
    const QString text = title.isEmpty() ? qtr( "VLC media player" ) : title;
    titleLabel->setText( text );
    titleLabel->setToolTip( text );
}

void TopBar::updateMaximizeButton()
{
    maximizeButton->setToolTip( mi->isMaximized() ? qtr( "Restore" ) : qtr( "Maximize" ) );
}

void TopBar::updateFolder()
{
    QString folder;
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
                folder = url.adjusted( QUrl::RemoveFilename | QUrl::StripTrailingSlash ).fileName();
            else
                folder = url.host();
        }
    }
    folderButton->setText( folder );
    folderButton->setVisible( !folder.isEmpty() );
}

void TopBar::showMainMenu()
{
    QWidget *button = qobject_cast<QWidget *>( sender() );
    mainMenu->exec( button ? button->mapToGlobal( QPoint( 0, button->height() ) )
                           : QCursor::pos() );
}
