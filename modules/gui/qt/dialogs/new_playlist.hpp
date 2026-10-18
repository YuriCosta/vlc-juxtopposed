/*****************************************************************************
 * new_playlist.hpp : "New Playlist" dialog of the redesign
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

#ifndef VLC_QT_NEW_PLAYLIST_HPP_
#define VLC_QT_NEW_PLAYLIST_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;

/* Figma "new playlist": title and thumbnail, then the files to put in it */
class NewPlaylistDialog : public QDialog
{
    Q_OBJECT

public:
    /* Returns the path of the created playlist, or an empty string */
    static QString create( intf_thread_t *, QWidget *parent,
                           const QStringList &trackUrls = QStringList() );

protected:
    void dragEnterEvent( QDragEnterEvent * ) Q_DECL_OVERRIDE;
    void dropEvent( QDropEvent * ) Q_DECL_OVERRIDE;
    bool eventFilter( QObject *, QEvent * ) Q_DECL_OVERRIDE;

private:
    NewPlaylistDialog( intf_thread_t *, QWidget *parent );

    void chooseImage();
    void chooseFiles();
    void addTracks( const QStringList &urls );
    void updateState();

    intf_thread_t *p_intf;
    QLineEdit   *nameEdit;
    QToolButton *imageButton;
    QWidget     *dropArea;
    QLabel      *dropLabel;
    QListWidget *fileList;
    QToolButton *saveButton;
    QString      imagePath;
    QStringList  tracks;
};

#endif
