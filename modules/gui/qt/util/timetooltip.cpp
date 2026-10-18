/*****************************************************************************
 * Copyright © 2011-2012 VideoLAN
 * $Id$
 *
 * Authors: Ludovic Fauvet <etix@l0cal.com>
 *
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "timetooltip.hpp"

#include <QApplication>
#include <QPainter>
#include <QBitmap>
#include <QFontMetrics>
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
#include <QDesktopWidget>
#else
#include <QScreen>
#endif

#define TIP_HEIGHT 5

/* Figma "thumbnail": 256x144 frame in a 8px padded card, time below */
#define CARD_PADDING   8
#define CARD_RADIUS    10
#define FRAME_WIDTH    256
#define FRAME_HEIGHT   144

TimeTooltip::TimeTooltip( QWidget *parent ) :
    QWidget( parent ), mTipX( -1 ), mPreview( false )
{
    setWindowFlags( Qt::ToolTip                 |
                    Qt::WindowStaysOnTopHint    |
                    Qt::FramelessWindowHint     );

    // Tell Qt that it doesn't need to erase the background before
    // a paintEvent occurs. This should save some CPU cycles.
    setAttribute( Qt::WA_OpaquePaintEvent );
    setAttribute( Qt::WA_TranslucentBackground );
    setAttribute( Qt::WA_TransparentForMouseEvents );

    mFont = qApp->font();
    mFont.setPixelSize( 15 );

    // By default the widget is unintialized and should not be displayed
    resize( 0, 0 );
}

void TimeTooltip::adjustPosition()
{
    if( mDisplayedText.isEmpty() )
    {
        resize( 0, 0 );
        return;
    }

    QFontMetrics metrics( mFont );
    const int textWidth = metrics.width( mDisplayedText ) + 2 * CARD_PADDING;
    const int textHeight = metrics.height() + 8;
    const QSize size = mPreview
        ? QSize( qMax( FRAME_WIDTH + 2 * CARD_PADDING, textWidth ),
                 CARD_PADDING + FRAME_HEIGHT + textHeight + CARD_PADDING / 2 )
        : QSize( textWidth + 8, textHeight + 4 );

    /* Right above the seek bar, centered on the hovered position */
    QPoint position( mTarget.x() - size.width() / 2, mTarget.y() - size.height() - 10 );

    // Keep the tooltip on the same screen if possible
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QRect screen{};
    auto *screenAt = QGuiApplication::screenAt( mTarget );
    if (screenAt != nullptr)
        screen = screenAt->geometry();
#else
    QRect screen = QApplication::desktop()->screenGeometry( mTarget );
#endif
    position.setX( qMax( screen.left(), qMin( position.x(),
        screen.left() + screen.width() - size.width() ) ) );
    position.setY( qMax( screen.top(), qMin( position.y(),
        screen.top() + screen.height() - size.height() ) ) );

    move( position );
    if( this->size() != size )
        resize( size );
    mBox = QRect( QPoint( 0, 0 ), size );
    buildPath();
}

void TimeTooltip::buildPath()
{
    mPainterPath = QPainterPath();
    mPainterPath.addRoundedRect( QRectF( mBox ).adjusted( 0.5, 0.5, -0.5, -0.5 ),
                                 mPreview ? CARD_RADIUS : 6, mPreview ? CARD_RADIUS : 6 );
}

void TimeTooltip::setTip( const QPoint& target, const QString& time, const QString& text )
{
    mDisplayedText = time;
    if ( !text.isEmpty() )
        mDisplayedText.append( " - " ).append( text );

    if( mTarget != target || time.length() != mTime.length() || mText != text )
    {
        mTarget = target;
        mTime = time;
        mText = text;
        adjustPosition();
    }

    update();
    raise();
}

void TimeTooltip::setPreview( bool b_preview, const QImage &image )
{
    const bool b_changed = b_preview != mPreview;
    mPreview = b_preview;
    mImage = image;
    if( b_changed )
        adjustPosition();
    update();
}

void TimeTooltip::show()
{
    setVisible( true );
    raise();
}

void TimeTooltip::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform );
    p.setCompositionMode( QPainter::CompositionMode_Source );
    p.fillRect( rect(), Qt::transparent );
    p.setCompositionMode( QPainter::CompositionMode_SourceOver );

    p.setPen( QColor( 0x22, 0x22, 0x22 ) );
    p.setBrush( QColor( 0x1B, 0x1B, 0x1B ) );
    p.drawPath( mPainterPath );

    QRect textRect = mBox;
    if( mPreview )
    {
        const QRect frame( ( mBox.width() - FRAME_WIDTH ) / 2, CARD_PADDING, FRAME_WIDTH, FRAME_HEIGHT );
        QPainterPath clip;
        clip.addRoundedRect( QRectF( frame ), 6, 6 );
        p.save();
        p.setClipPath( clip );
        if( mImage.isNull() )
            p.fillRect( frame, QColor( 0x30, 0x30, 0x30 ) );
        else
            p.drawImage( frame, mImage );
        p.restore();
        textRect.setTop( frame.bottom() + 2 );
    }

    p.setFont( mFont );
    p.setPen( QColor( 0x8F, 0x8F, 0x8F ) );
    p.drawText( textRect, Qt::AlignCenter, mDisplayedText );
}
