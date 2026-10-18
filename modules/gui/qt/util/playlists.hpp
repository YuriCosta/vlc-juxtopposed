/*****************************************************************************
 * playlists.hpp : the user's saved playlists (XSPF files)
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

#ifndef VLC_QT_PLAYLISTS_HPP_
#define VLC_QT_PLAYLISTS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QString>
#include <QStringList>

/* In the redesign the queue is what plays next, and playlists are collections
 * the user keeps: one XSPF file each (title, thumbnail and tracks) in the
 * "playlists" folder of the VLC user data, which VLC itself can open. */
namespace Playlists
{
    QString folder( intf_thread_t * );
    QStringList list( intf_thread_t * );   /* paths of the playlist files */

    struct Info
    {
        QString     title;
        QString     image;      /* local path, may be empty */
        QStringList tracks;     /* URLs */
    };
    Info read( const QString &path );

    /* Returns the path of the new playlist, or an empty string on failure */
    QString create( intf_thread_t *, const QString &title, const QString &image,
                    const QStringList &trackUrls );
    bool append( const QString &path, const QStringList &trackUrls );
    bool removeTrack( const QString &path, int index );
    bool remove( const QString &path );
}

#endif
