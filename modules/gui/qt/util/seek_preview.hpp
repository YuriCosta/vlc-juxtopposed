/*****************************************************************************
 * seek_preview.hpp : frames of the media for the seek bar tooltip
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

#ifndef VLC_QT_SEEK_PREVIEW_HPP_
#define VLC_QT_SEEK_PREVIEW_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>

class QTimer;
struct SeekPreviewJob;

/* VLC 3 has no thumbnailer: a second, silent input of the same media is
 * started at the hovered time, its first frame is grabbed through the "vmem"
 * video output and the input is closed. Requests are debounced, one at a
 * time, and the frames are cached by 2 s slices. */
class SeekPreviewer : public QObject
{
    Q_OBJECT

public:
    static SeekPreviewer *instance( intf_thread_t * );
    static void killInstance();

    /* The frame if it is known, a null image otherwise (it is then requested
     * and previewReady() is emitted once available) */
    QImage preview( const QString &mrl, int64_t time );
    void cancel();

    static const int WIDTH  = 256;
    static const int HEIGHT = 144;

signals:
    void previewReady();

private slots:
    void startJob();
    void deliver( const QString &key, const QImage &image );

private:
    SeekPreviewer( intf_thread_t * );
    virtual ~SeekPreviewer();
    static QString cacheKey( const QString &mrl, int64_t time );
    void stopJob();

    intf_thread_t      *p_intf;
    QHash<QString, QImage> cache;
    QStringList         cacheOrder;
    QString             pendingMrl;
    int64_t             pendingTime;
    QTimer             *debounce;
    QTimer             *watchdog;
    SeekPreviewJob     *job;
};

#endif
