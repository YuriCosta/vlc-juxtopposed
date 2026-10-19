/*****************************************************************************
 * shortcuts_dialog.hpp : keyboard shortcuts of the redesigned interface
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

#ifndef VLC_QT_SHORTCUTS_DIALOG_HPP_
#define VLC_QT_SHORTCUTS_DIALOG_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QDialog>
#include <QList>

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;
class ShortcutField;

/* Figma "Keyboard Shortcuts": the hotkeys and global hotkeys of every
 * action, by group, edited by clicking them */
class ShortcutsDialog : public QDialog
{
    Q_OBJECT

public:
    static void open( intf_thread_t *, QWidget *parent );
    /* Every hotkey back to its default value */
    static void resetAll( intf_thread_t * );

private:
    ShortcutsDialog( intf_thread_t *, QWidget *parent );

    struct Shortcut
    {
        QString name;   /* "key-play-pause", the global one is "global-" + name */
        QString text;
        QString keys[2];   /* hotkey, global hotkey: VLC key names, '\t' separated */
        QString saved[2];
        ShortcutField *fields[2];
        QWidget *row;
        QWidget *group;
    };

    void load();
    void capture( int index, int column, unsigned key );
    void clear( int index, int column );
    void filter();
    void updateState();
    void save();

    intf_thread_t *p_intf;
    QList<Shortcut> shortcuts;
    /* group title and the divider above it */
    QList<QPair<QWidget *, QWidget *> > groups;
    QLineEdit   *searchEdit;
    QComboBox   *fieldBox;
    QLabel      *noteLabel;
    QToolButton *saveButton;
};

#endif
