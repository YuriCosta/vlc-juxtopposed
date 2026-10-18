/*****************************************************************************
 * settings_dialog.hpp : settings of the redesigned interface
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

#ifndef VLC_QT_SETTINGS_DIALOG_HPP_
#define VLC_QT_SETTINGS_DIALOG_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QDialog>
#include <QList>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QStackedWidget;
class SettingsPage;

/* Figma "Settings - *": categories on the left, one page of the most useful
 * options each, the less common ones behind "Advanced Options". Changes are
 * applied at once and saved when the window closes. The complete tree of
 * options stays available from General. */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum Category
    {
        CAT_GENERAL, CAT_INTERFACE, CAT_PRIVACY, CAT_CONTROLS,
        CAT_PLAYBACK, CAT_VIDEO, CAT_AUDIO, CAT_SUBTITLES, CAT_LIBRARY,
        CAT_NETWORK, CAT_PERFORMANCE, CAT_RECORDING, CAT_EXTENSIONS,
    };

    static void open( intf_thread_t *, QWidget *parent, int category = -1 );

protected:
    void done( int ) Q_DECL_OVERRIDE;

private:
    SettingsDialog( intf_thread_t *, QWidget *parent );

    void addCategory( int id, const QString &icon, const QString &name, SettingsPage * );
    void addGroup( const QString & );
    void showCategory( int );
    void filter( const QString & );

    SettingsPage *generalPage();
    SettingsPage *interfacePage();
    SettingsPage *privacyPage();
    SettingsPage *controlsPage();
    SettingsPage *playbackPage();
    SettingsPage *videoPage();
    SettingsPage *audioPage();
    SettingsPage *subtitlesPage();
    SettingsPage *libraryPage();
    SettingsPage *networkPage();
    SettingsPage *performancePage();
    SettingsPage *recordingPage();
    SettingsPage *extensionsPage();

    intf_thread_t  *p_intf;
    QWidget        *categoryList;
    QButtonGroup   *categories;
    QStackedWidget *pages;
    QLabel         *titleLabel;
    QLineEdit      *searchEdit;
    QList<SettingsPage *> pageList;
};

#endif
