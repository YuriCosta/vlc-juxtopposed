/*****************************************************************************
 * thumbnailer.hpp : asynchronous media thumbnails for the redesigned library
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

#ifndef VLC_QT_THUMBNAILER_HPP_
#define VLC_QT_THUMBNAILER_HPP_

/* No QThread here: vlc_threads.h defines msleep() as a macro */
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

class ThumbnailerThread;

/* Thumbnails of local media files. On Windows they come from the shell
 * (the same ones as the file explorer), elsewhere none are provided and
 * callers keep their generic icons. Requests are served by a worker thread. */
class Thumbnailer : public QObject
{
    Q_OBJECT

public:
    static Thumbnailer *instance();
    static void killInstance();

    /* Returns the thumbnail if known, otherwise queues a request and returns
     * a null pixmap; thumbnailReady() is emitted once it is available */
    QPixmap thumbnail( const QString &path );

signals:
    void thumbnailReady( const QString &path );

private slots:
    void store( const QString &path, const QImage &image );

private:
    Thumbnailer();
    virtual ~Thumbnailer();

    QHash<QString, QPixmap> cache;
    QSet<QString>           failed;
    QSet<QString>           requested;
    ThumbnailerThread      *worker;
};

#endif
