/*****************************************************************************
 * overlay_panels.hpp : track and settings panels of the redesigned interface
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

#ifndef VLC_QT_OVERLAY_PANELS_HPP_
#define VLC_QT_OVERLAY_PANELS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QFrame>
#include <QList>

#include <functional>

class QButtonGroup;
class QComboBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;

/* A panel floating over the video, above the right end of the control bar
 * (Figma "Controls" variants), with tabs on top */
class OverlayPanel : public QFrame
{
    Q_OBJECT

public:
    OverlayPanel( intf_thread_t *, QWidget *parent, QWidget *area, bool b_fullHeight );

    void showTab( int );
    int currentTab() const;

public slots:
    virtual void refresh() = 0;

signals:
    void visibilityChanged( bool );

protected:
    /* Adds a tab and returns the layout of its page */
    QVBoxLayout *addTab( const QString &icon, const QString &text );
    void updateGeometryFromArea();

    bool eventFilter( QObject *, QEvent * ) Q_DECL_OVERRIDE;
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;
    void showEvent( QShowEvent * ) Q_DECL_OVERRIDE;
    void hideEvent( QHideEvent * ) Q_DECL_OVERRIDE;
    void keyPressEvent( QKeyEvent * ) Q_DECL_OVERRIDE;
    void wheelEvent( QWheelEvent * ) Q_DECL_OVERRIDE;

    intf_thread_t  *p_intf;
    QWidget        *area;
    bool            b_fullHeight;
    QWidget        *tabBar;
    QButtonGroup   *tabs;
    QStackedWidget *pages;
    QVBoxLayout    *footer;
};

/* Figma "Controls / audio, subtitles, video": pick the tracks and the audio
 * output without going through the menus */
class TracksPanel : public OverlayPanel
{
    Q_OBJECT

public:
    enum { TAB_VIDEO, TAB_AUDIO, TAB_SUBTITLES };
    TracksPanel( intf_thread_t *, QWidget *parent, QWidget *area );

public slots:
    void refresh() Q_DECL_OVERRIDE;

signals:
    void preferencesRequested( int settingsTab );

private:
    void fillTracks( QVBoxLayout *, const char *var, const QString &title );
    void fillAudioDevices( QVBoxLayout * );

    QList<QVBoxLayout *> tabLayouts;
};

/* Figma "Controls / playback, video settings, audio settings, subtitle
 * settings": the settings needed while watching, applied live */
class SettingsPanel : public OverlayPanel
{
    Q_OBJECT

public:
    enum { TAB_PLAYBACK, TAB_VIDEO, TAB_AUDIO, TAB_SUBTITLES };
    SettingsPanel( intf_thread_t *, QWidget *parent, QWidget *area );

public slots:
    void refresh() Q_DECL_OVERRIDE;

private:
    void buildPlayback( QVBoxLayout * );
    void buildVideo( QVBoxLayout * );
    void buildAudio( QVBoxLayout * );
    void buildSubtitles( QVBoxLayout * );
    void fillChoices( QComboBox *, vlc_object_t *, const char *var );

    QList<std::function<void()>> refreshers;
    QTimer *sleepTimer;
};

#endif
