/*****************************************************************************
 * queue_panel.hpp : floating play queue of the redesigned interface
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

#ifndef VLC_QT_QUEUE_PANEL_HPP_
#define VLC_QT_QUEUE_PANEL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QFrame>

class QLabel;
class QListView;
class QToolButton;
class PLModel;

/* Figma "Controls / queue": a 400px panel floating over the video, above the
 * bottom-right of the control bar, listing what will be played next.
 * The queue is VLC's playlist ("Playlist" node). */
class QueuePanel : public QFrame
{
    Q_OBJECT

public:
    /* area: the widget whose geometry the panel floats over (the video) */
    QueuePanel( intf_thread_t *, QWidget *parent, QWidget *area );

protected:
    bool eventFilter( QObject *, QEvent * ) Q_DECL_OVERRIDE;
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;
    void showEvent( QShowEvent * ) Q_DECL_OVERRIDE;
    void hideEvent( QHideEvent * ) Q_DECL_OVERRIDE;
    void keyPressEvent( QKeyEvent * ) Q_DECL_OVERRIDE;
    void wheelEvent( QWheelEvent * ) Q_DECL_OVERRIDE;

signals:
    void visibilityChanged( bool );

private slots:
    void updateSummary();
    void updateGeometryFromArea();
    void activate( const QModelIndex & );
    void removeSelected();
    void showSortMenu();
    void setRandom( bool );
    void setRepeat( int );

private:
    QToolButton *createButton( const QString &icon, const QString &tooltip );

    intf_thread_t *p_intf;
    QWidget       *area;
    PLModel       *model;
    QListView     *view;
    QLabel        *summaryLabel;
    QToolButton   *repeatButton;
    QToolButton   *shuffleButton;
};

#endif
