/*****************************************************************************
 * network_stream.hpp : "Open Network Stream" dialog of the redesign
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

#ifndef VLC_QT_NETWORK_STREAM_HPP_
#define VLC_QT_NETWORK_STREAM_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QPlainTextEdit;
class QToolButton;

/* Figma "Open network stream": one link per line, optionally saved as a
 * playlist of the library */
class NetworkStreamDialog : public QDialog
{
    Q_OBJECT

public:
    static void open( intf_thread_t *, QWidget *parent );

private:
    NetworkStreamDialog( intf_thread_t *, QWidget *parent );

    QStringList urls() const;
    void updateState();
    void play();

    intf_thread_t  *p_intf;
    QPlainTextEdit *urlEdit;
    QLineEdit      *nameEdit;
    QToolButton    *openButton;
};

#endif
