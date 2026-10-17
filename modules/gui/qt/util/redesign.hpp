/*****************************************************************************
 * redesign.hpp : design tokens and global style of the redesigned interface
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

#ifndef VLC_QT_REDESIGN_HPP_
#define VLC_QT_REDESIGN_HPP_

#include <QColor>
#include <QString>

class QTimer;
class QWidget;

/* Colors from the "VLC Redesign (Community)" Figma file (Dark theme) */
namespace Redesign
{
    /* Backgrounds */
    static const QColor BgA3       ( 0x1B, 0x1B, 0x1B ); /* bars, panels */
    static const QColor BgA4       ( 0x19, 0x19, 0x19 );
    static const QColor BgA5       ( 0x15, 0x15, 0x15 ); /* inputs */
    static const QColor BgB1       ( 0x0E, 0x0E, 0x0E ); /* window background */
    static const QColor BgB2       ( 0x09, 0x09, 0x09 ); /* slider tracks */
    static const QColor BgAccent   ( 0x37, 0x1F, 0x06 ); /* elapsed part of the seek bar */

    /* Foregrounds */
    static const QColor FgA1       ( 0x96, 0x96, 0x96 ); /* emphasized text */
    static const QColor FgA2       ( 0x8F, 0x8F, 0x8F ); /* text and icons */
    static const QColor FgA3       ( 0x6B, 0x6B, 0x6B ); /* secondary text */
    static const QColor FgA4       ( 0x40, 0x40, 0x40 ); /* placeholders */
    static const QColor FgB1       ( 0x16, 0x16, 0x16 ); /* content on the accent color */

    static const QColor Accent     ( 0xD3, 0x79, 0x18 ); /* VLC orange */
    static const QColor DividerBright( 0x37, 0x37, 0x37 );
    static const QColor DividerDim ( 0x22, 0x22, 0x22 );

    /* Application wide style sheet, applied on top of the dark palette */
    QString styleSheet();

    /* Floating panels close when the mouse is pressed anywhere else, the video
     * included (it is a native window Qt gets no events from). Returns the
     * watching timer: start it when the panel is shown, stop it when hidden.
     * Widgets with the "panelToggle" property (the buttons opening the panels)
     * are ignored, they toggle the panel themselves. */
    QTimer *outsideClickWatcher( QWidget *panel );
}

#endif
