/*****************************************************************************
 * add_subtitles.hpp : subtitles for a file, a folder or the queue
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

#ifndef VLC_QT_ADD_SUBTITLES_HPP_
#define VLC_QT_ADD_SUBTITLES_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QDialog>
#include <QList>
#include <QPair>
#include <QStringList>

class QComboBox;
class QListWidget;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

/* Figma "Add Subtitles": instead of dragging a subtitle file on every episode,
 * pick the subtitles of this file, or match a whole folder (or the queue) to a
 * folder of subtitles once. Matches are remembered and loaded automatically
 * when each episode is opened. */
class AddSubtitlesDialog : public QDialog
{
    Q_OBJECT

public:
    AddSubtitlesDialog( intf_thread_t *, QWidget *parent );

    static void open( intf_thread_t *, QWidget *parent );
    /* Loads the remembered subtitles of the media being played */
    static void applySavedSubtitles( intf_thread_t * );

private slots:
    void rebuild();
    void chooseFolder();
    void save();

private:
    enum { TARGET_FILE, TARGET_FOLDER, TARGET_QUEUE };

    QStringList targetMedia() const;
    void updateFolderButton();

    intf_thread_t  *p_intf;
    QString         mediaPath;     /* media being played */
    QString         subFolder;     /* where the subtitles are */
    QComboBox      *targetCombo;
    QComboBox      *sourceCombo;
    QToolButton    *folderButton;
    QStackedWidget *pages;
    QListWidget    *fileList;
    QVBoxLayout    *matchLayout;
    QList<QPair<QString, QComboBox *> > matches;
};

#endif
