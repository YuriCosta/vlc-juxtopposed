/*****************************************************************************
 * overlay_panels.cpp : track and settings panels of the redesigned interface
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "components/overlay_panels.hpp"
#include "input_manager.hpp"
#include "dialogs_provider.hpp"
#include "util/redesign.hpp"

#include <vlc_aout.h>
#include <vlc_vout.h>
#include <vlc_playlist.h>
#include "../../audio_filter/equalizer_presets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#define PANEL_WIDTH  400
#define PANEL_MARGIN 20
#define PANEL_RADIUS 10

namespace {

QPixmap checkPixmap( bool b_checked )
{
    QPixmap pix( 16, 16 );
    pix.fill( Qt::transparent );
    if( b_checked )
    {
        QPainter p( &pix );
        p.setRenderHint( QPainter::Antialiasing );
        p.setPen( QPen( Redesign::Accent, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
        const QPointF points[] = { QPointF( 2.5, 8.5 ), QPointF( 6.5, 12.5 ), QPointF( 13.5, 4 ) };
        p.drawPolyline( points, 3 );
    }
    return pix;
}

/* "✓ English": one selectable entry of a list */
QToolButton *choiceRow( const QString &text, bool b_checked )
{
    QToolButton *row = new QToolButton;
    row->setObjectName( "panelChoice" );
    row->setText( text );
    row->setIcon( QIcon( checkPixmap( b_checked ) ) );
    row->setIconSize( QSize( 16, 16 ) );
    row->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    row->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    row->setFixedHeight( 32 );
    row->setFocusPolicy( Qt::NoFocus );
    row->setProperty( "checked", b_checked );
    return row;
}

QLabel *sectionLabel( const QString &text, bool b_strong = false )
{
    QLabel *label = new QLabel( text );
    label->setObjectName( b_strong ? "panelSectionStrong" : "panelSection" );
    return label;
}

QFrame *divider()
{
    QFrame *line = new QFrame;
    line->setObjectName( "panelDivider" );
    line->setFixedHeight( 1 );
    return line;
}

/* "Label ........ [widget]" */
QHBoxLayout *labeledRow( const QString &text, QWidget *widget )
{
    QHBoxLayout *row = new QHBoxLayout;
    row->setContentsMargins( 0, 0, 0, 0 );
    QLabel *label = new QLabel( text );
    label->setObjectName( "panelLabel" );
    row->addWidget( label, 1 );
    widget->setFixedWidth( 150 );
    row->addWidget( widget );
    return row;
}

QToolButton *footerButton( const QString &text )
{
    QToolButton *button = new QToolButton;
    button->setObjectName( "panelFooterButton" );
    button->setText( text );
    button->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    button->setFixedHeight( 32 );
    button->setFocusPolicy( Qt::NoFocus );
    return button;
}

void clearLayout( QLayout *layout )
{
    QLayoutItem *item;
    while( ( item = layout->takeAt( 0 ) ) )
    {
        if( item->layout() )
            clearLayout( item->layout() );
        delete item->widget();
        delete item;
    }
}

QString formatDelay( int ms )
{
    return QString::number( ms / 1000.0, 'f', 3 ) + " s";
}

} // namespace

/*****************************************************************************
 * OverlayPanel
 *****************************************************************************/
OverlayPanel::OverlayPanel( intf_thread_t *_p_intf, QWidget *parent, QWidget *_area, bool _b_fullHeight )
            : QFrame( parent ), p_intf( _p_intf ), area( _area ), b_fullHeight( _b_fullHeight )
{
    /* Native, so that it can be stacked above the (native) video output */
    setAttribute( Qt::WA_NativeWindow );
    setFocusPolicy( Qt::StrongFocus );
    setObjectName( "overlayPanel" );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    tabBar = new QWidget;
    tabBar->setObjectName( "panelTabs" );
    tabBar->setFixedHeight( 50 );
    QHBoxLayout *tabsLayout = new QHBoxLayout( tabBar );
    tabsLayout->setContentsMargins( 6, 0, 6, 0 );
    tabsLayout->setSpacing( 1 );
    layout->addWidget( tabBar );

    tabs = new QButtonGroup( this );
    tabs->setExclusive( true );
    pages = new QStackedWidget;
    layout->addWidget( pages, 1 );
    connect( tabs, static_cast<void (QButtonGroup::*)(int)>( &QButtonGroup::buttonClicked ),
             this, [this]( int id ) { pages->setCurrentIndex( id ); updateGeometryFromArea(); } );

    QWidget *footerWidget = new QWidget;
    footer = new QVBoxLayout( footerWidget );
    footer->setContentsMargins( 10, 6, 10, 10 );
    footer->setSpacing( 6 );
    layout->addWidget( footerWidget );

    area->installEventFilter( this );
    QTimer *watcher = Redesign::outsideClickWatcher( this );
    connect( this, &OverlayPanel::visibilityChanged, watcher,
             [watcher]( bool b ) { if( b ) watcher->start(); else watcher->stop(); } );
    hide();
}

/* Never let the wheel reach the main window, which turns it into volume */
void OverlayPanel::wheelEvent( QWheelEvent *event )
{
    event->accept();
}

QVBoxLayout *OverlayPanel::addTab( const QString &icon, const QString &text )
{
    QToolButton *tab = new QToolButton;
    tab->setObjectName( "panelTab" );
    tab->setIcon( QIcon( icon ) );
    tab->setIconSize( QSize( 20, 20 ) );
    tab->setText( text );
    tab->setCheckable( true );
    tab->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    tab->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    tab->setFocusPolicy( Qt::NoFocus );
    const int id = tabs->buttons().count();
    tabs->addButton( tab, id );
    tabBar->layout()->addWidget( tab );
    if( id == 0 )
        tab->setChecked( true );

    /* Long pages scroll */
    QScrollArea *scroll = new QScrollArea;
    scroll->setObjectName( "panelScroll" );
    scroll->setFrameShape( QFrame::NoFrame );
    scroll->setWidgetResizable( true );
    scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    QWidget *page = new QWidget;
    page->setObjectName( "panelPage" );
    QVBoxLayout *pageLayout = new QVBoxLayout( page );
    pageLayout->setContentsMargins( 20, 16, 20, 16 );
    pageLayout->setSpacing( 6 );
    scroll->setWidget( page );
    pages->addWidget( scroll );
    return pageLayout;
}

void OverlayPanel::showTab( int id )
{
    if( tabs->button( id ) )
        tabs->button( id )->setChecked( true );
    pages->setCurrentIndex( id );
    if( isVisible() )
        updateGeometryFromArea();
}

int OverlayPanel::currentTab() const
{
    return pages->currentIndex();
}

void OverlayPanel::updateGeometryFromArea()
{
    if( !parentWidget() )
        return;
    const QRect a( area->mapTo( parentWidget(), QPoint( 0, 0 ) ), area->size() );
    const int w = qMax( 200, qMin( PANEL_WIDTH, a.width() - 2 * PANEL_MARGIN ) );
    const int maxHeight = qMax( 150, a.height() - 2 * PANEL_MARGIN );
    int h = maxHeight;
    if( !b_fullHeight )
    {
        /* Track lists only take the room they need */
        QScrollArea *scroll = qobject_cast<QScrollArea *>( pages->currentWidget() );
        int content = 300;
        if( scroll && scroll->widget() && scroll->widget()->layout() )
        {
            scroll->widget()->layout()->activate();
            content = scroll->widget()->layout()->minimumSize().height();
        }
        h = qMin( maxHeight, tabBar->height() + content + layout()->itemAt( 2 )->sizeHint().height() + 4 );
    }
    /* anchored to the bottom, right above the control bar */
    setGeometry( a.right() - PANEL_MARGIN - w + 1, a.bottom() - PANEL_MARGIN - h + 1, w, h );
    raise();
}

bool OverlayPanel::eventFilter( QObject *obj, QEvent *event )
{
    if( obj == area && isVisible() &&
        ( event->type() == QEvent::Resize || event->type() == QEvent::Move ) )
        updateGeometryFromArea();
    return QFrame::eventFilter( obj, event );
}

void OverlayPanel::resizeEvent( QResizeEvent *event )
{
    QFrame::resizeEvent( event );
    /* A native child cannot be translucent: clip the rounded corners */
    QPainterPath path;
    path.addRoundedRect( QRectF( rect() ), PANEL_RADIUS, PANEL_RADIUS );
    setMask( QRegion( path.toFillPolygon().toPolygon() ) );
}

void OverlayPanel::showEvent( QShowEvent *event )
{
    refresh();
    QFrame::showEvent( event );
    updateGeometryFromArea();
    setFocus();
    emit visibilityChanged( true );
}

void OverlayPanel::hideEvent( QHideEvent *event )
{
    QFrame::hideEvent( event );
    emit visibilityChanged( false );
}

void OverlayPanel::keyPressEvent( QKeyEvent *event )
{
    if( event->key() == Qt::Key_Escape )
        hide();
    else
        QFrame::keyPressEvent( event );
}

/*****************************************************************************
 * TracksPanel
 *****************************************************************************/
TracksPanel::TracksPanel( intf_thread_t *_p_intf, QWidget *parent, QWidget *_area )
           : OverlayPanel( _p_intf, parent, _area, false )
{
    tabLayouts << addTab( ":/redesign/lib-videos.svg", qtr( "Video" ) );
    tabLayouts << addTab( ":/toolbar/audio_tracks.svg", qtr( "Audio" ) );
    tabLayouts << addTab( ":/toolbar/subtitle_tracks.svg", qtr( "Subtitles" ) );

    QToolButton *prefs = footerButton( qtr( "Show Preferences" ) );
    connect( prefs, &QToolButton::clicked, this, [this]() {
        static const int map[] = { SettingsPanel::TAB_VIDEO, SettingsPanel::TAB_AUDIO,
                                   SettingsPanel::TAB_SUBTITLES };
        emit preferencesRequested( map[ qBound( 0, currentTab(), 2 ) ] );
    } );
    footer->addWidget( prefs );

    connect( THEMIM, &MainInputManager::inputChanged, this, [this]() { if( isVisible() ) refresh(); } );
}

void TracksPanel::fillTracks( QVBoxLayout *layout, const char *var, const QString &title )
{
    layout->addWidget( sectionLabel( title ) );
    input_thread_t *p_input = THEMIM->getInput();
    vlc_value_t val, text;
    if( !p_input || var_Change( p_input, var, VLC_VAR_GETCHOICES, &val, &text ) < 0 )
    {
        QLabel *none = new QLabel( qtr( "Nothing is playing" ) );
        none->setObjectName( "panelLabel" );
        layout->addWidget( none );
        return;
    }

    const int64_t current = var_GetInteger( p_input, var );
    for( int i = 0; i < val.p_list->i_count; i++ )
    {
        const int64_t id = val.p_list->p_values[i].i_int;
        const char *psz_name = text.p_list->p_values[i].psz_string;
        QString name = psz_name ? qfu( psz_name ) : qtr( "Track %1" ).arg( id );
        if( id < 0 )
            name = qtr( "None" );
        QToolButton *row = choiceRow( name, id == current );
        const QByteArray varName( var );
        connect( row, &QToolButton::clicked, this, [this, varName, id]() {
            input_thread_t *p_in = THEMIM->getInput();
            if( p_in )
                var_SetInteger( p_in, varName.constData(), id );
            refresh();
        } );
        layout->addWidget( row );
    }
    var_FreeList( &val, &text );
}

void TracksPanel::fillAudioDevices( QVBoxLayout *layout )
{
    layout->addWidget( divider() );
    layout->addWidget( sectionLabel( qtr( "Output Device" ) ) );
    audio_output_t *p_aout = THEMIM->getAout();
    if( !p_aout )
        return;

    char **ids, **names;
    const int count = aout_DevicesList( p_aout, &ids, &names );
    char *selected = aout_DeviceGet( p_aout );
    for( int i = 0; i < count; i++ )
    {
        const bool b_selected = ( selected && !strcmp( ids[i], selected ) ) ||
                                ( !selected && ids[i] && ids[i][0] == '\0' );
        QToolButton *row = choiceRow( qfue( names[i] ), b_selected );
        const QByteArray id( ids[i] );
        connect( row, &QToolButton::clicked, this, [this, id]() {
            audio_output_t *aout = THEMIM->getAout();
            if( aout )
            {
                aout_DeviceSet( aout, id.isEmpty() ? NULL : id.constData() );
                vlc_object_release( aout );
            }
            refresh();
        } );
        layout->addWidget( row );
        free( ids[i] );
        free( names[i] );
    }
    if( count > 0 )
    {
        free( ids );
        free( names );
    }
    free( selected );
    vlc_object_release( p_aout );
}

void TracksPanel::refresh()
{
    foreach( QVBoxLayout *layout, tabLayouts )
        clearLayout( layout );

    fillTracks( tabLayouts[TAB_VIDEO], "video-es", qtr( "Video Tracks" ) );

    fillTracks( tabLayouts[TAB_AUDIO], "audio-es", qtr( "Audio Tracks" ) );
    fillAudioDevices( tabLayouts[TAB_AUDIO] );

    fillTracks( tabLayouts[TAB_SUBTITLES], "spu-es", qtr( "Subtitle Tracks" ) );
    tabLayouts[TAB_SUBTITLES]->addWidget( divider() );
    QToolButton *addSub = choiceRow( qtr( "Add a subtitle file..." ), false );
    addSub->setIcon( QIcon( ":/redesign/lib-add-small.svg" ) );
    connect( addSub, &QToolButton::clicked, this, [this]() { THEDP->loadSubtitlesFile(); } );
    tabLayouts[TAB_SUBTITLES]->addWidget( addSub );

    foreach( QVBoxLayout *layout, tabLayouts )
        layout->addStretch( 1 );
    /* measure once the new rows have been laid out */
    QTimer::singleShot( 0, this, [this]() { if( isVisible() ) updateGeometryFromArea(); } );
}

/*****************************************************************************
 * SettingsPanel
 *****************************************************************************/
SettingsPanel::SettingsPanel( intf_thread_t *_p_intf, QWidget *parent, QWidget *_area )
             : OverlayPanel( _p_intf, parent, _area, true )
{
    sleepTimer = new QTimer( this );
    sleepTimer->setSingleShot( true );
    connect( sleepTimer, &QTimer::timeout, THEMIM, &MainInputManager::pause );

    buildPlayback( addTab( ":/toolbar/play_b.svg", qtr( "Playback" ) ) );
    buildVideo( addTab( ":/redesign/lib-videos.svg", qtr( "Video" ) ) );
    buildAudio( addTab( ":/toolbar/audio_tracks.svg", qtr( "Audio" ) ) );
    buildSubtitles( addTab( ":/toolbar/subtitle_tracks.svg", qtr( "Subtitles" ) ) );

    QToolButton *advanced = footerButton( qtr( "Open Advanced Settings" ) );
    advanced->setIcon( QIcon( ":/toolbar/extended.svg" ) );
    advanced->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    connect( advanced, &QToolButton::clicked, this, [this]() { hide(); THEDP->prefsDialog(); } );
    footer->addWidget( advanced );

    connect( THEMIM, &MainInputManager::inputChanged, this, [this]() { if( isVisible() ) refresh(); } );
}

void SettingsPanel::refresh()
{
    foreach( const std::function<void()> &refresher, refreshers )
        refresher();
}

/* Fills a combo with the choices of a variable and selects the current one */
void SettingsPanel::fillChoices( QComboBox *combo, vlc_object_t *obj, const char *var )
{
    const QSignalBlocker blocker( combo );
    combo->clear();
    vlc_value_t val, text, current;
    if( !obj || var_Change( obj, var, VLC_VAR_GETCHOICES, &val, &text ) < 0 )
    {
        combo->setEnabled( false );
        return;
    }
    combo->setEnabled( true );
    const int type = var_Type( obj, var ) & VLC_VAR_CLASS;
    var_Get( obj, var, &current );
    for( int i = 0; i < val.p_list->i_count; i++ )
    {
        const vlc_value_t &v = val.p_list->p_values[i];
        const char *psz_text = text.p_list->p_values[i].psz_string;
        QVariant data;
        bool b_current = false;
        switch( type )
        {
        case VLC_VAR_STRING:
            data = qfu( v.psz_string );
            b_current = current.psz_string && !strcmp( current.psz_string, v.psz_string );
            break;
        case VLC_VAR_INTEGER:
            data = (qlonglong)v.i_int;
            b_current = current.i_int == v.i_int;
            break;
        case VLC_VAR_FLOAT:
            data = v.f_float;
            b_current = current.f_float == v.f_float;
            break;
        default:
            continue;
        }
        combo->addItem( psz_text ? qfu( psz_text ) : data.toString(), data );
        if( b_current )
            combo->setCurrentIndex( combo->count() - 1 );
    }
    if( type == VLC_VAR_STRING )
        free( current.psz_string );
    var_FreeList( &val, &text );
}

/* Applies a combo choice to a variable of the current vout or aout */
static void applyChoice( vlc_object_t *obj, const char *var, const QVariant &data )
{
    if( !obj )
        return;
    switch( var_Type( obj, var ) & VLC_VAR_CLASS )
    {
    case VLC_VAR_STRING:  var_SetString( obj, var, qtu( data.toString() ) ); break;
    case VLC_VAR_INTEGER: var_SetInteger( obj, var, data.toLongLong() ); break;
    case VLC_VAR_FLOAT:   var_SetFloat( obj, var, data.toFloat() ); break;
    default: break;
    }
}

void SettingsPanel::buildPlayback( QVBoxLayout *layout )
{
    layout->addWidget( sectionLabel( qtr( "Speed" ), true ) );
    QLabel *speedValue = new QLabel;
    speedValue->setObjectName( "panelValue" );
    layout->addLayout( labeledRow( qtr( "Playback speed" ), speedValue ) );
    QSlider *speed = new QSlider( Qt::Horizontal );
    speed->setObjectName( "panelSlider" );
    speed->setRange( 25, 400 );
    speed->setSingleStep( 5 );
    speed->setPageStep( 25 );
    layout->addWidget( speed );
    connect( speed, &QSlider::valueChanged, this, [this, speedValue]( int v ) {
        speedValue->setText( QString::number( v / 100.0, 'f', 2 ) + "x" );
    } );
    connect( speed, &QSlider::sliderReleased, this, [this, speed]() {
        var_SetFloat( THEPL, "rate", speed->value() / 100.f );
    } );
    /* applied when released while dragging, immediately for clicks and keys */
    connect( speed, &QSlider::valueChanged, this, [this, speed]( int v ) {
        if( !speed->isSliderDown() )
            var_SetFloat( THEPL, "rate", v / 100.f );
    } );
    QToolButton *normal = footerButton( qtr( "Normal speed" ) );
    connect( normal, &QToolButton::clicked, this, [this, speed]() { speed->setValue( 100 ); } );
    layout->addWidget( normal );
    refreshers << [speed, speedValue, this]() {
        const QSignalBlocker blocker( speed );
        const int v = qRound( var_GetFloat( THEPL, "rate" ) * 100 );
        speed->setValue( v );
        speedValue->setText( QString::number( v / 100.0, 'f', 2 ) + "x" );
    };

    layout->addWidget( divider() );
    layout->addWidget( sectionLabel( qtr( "Sleep Timer" ), true ) );
    QComboBox *sleep = new QComboBox;
    sleep->setObjectName( "panelCombo" );
    sleep->addItem( qtr( "Off" ), 0 );
    foreach( int minutes, QList<int>() << 15 << 30 << 45 << 60 << 90 << 120 )
        sleep->addItem( qtr( "%1 minutes" ).arg( minutes ), minutes );
    layout->addLayout( labeledRow( qtr( "Pause after" ), sleep ) );
    connect( sleep, static_cast<void (QComboBox::*)(int)>( &QComboBox::activated ), this, [this, sleep]( int i ) {
        const int minutes = sleep->itemData( i ).toInt();
        if( minutes > 0 )
            sleepTimer->start( minutes * 60 * 1000 );
        else
            sleepTimer->stop();
    } );
    connect( sleepTimer, &QTimer::timeout, sleep, [this, sleep]() { sleep->setCurrentIndex( 0 ); } );

    layout->addWidget( divider() );
    layout->addWidget( sectionLabel( qtr( "Media" ), true ) );
    QToolButton *info = footerButton( qtr( "Media information and metadata" ) );
    connect( info, &QToolButton::clicked, this, [this]() { THEDP->mediaInfoDialog(); } );
    layout->addWidget( info );
    QToolButton *bookmarks = footerButton( qtr( "Bookmarks" ) );
    connect( bookmarks, &QToolButton::clicked, this, [this]() { THEDP->bookmarksDialog(); } );
    layout->addWidget( bookmarks );
    QToolButton *jump = footerButton( qtr( "Jump to time" ) );
    connect( jump, &QToolButton::clicked, this, [this]() { THEDP->gotoTimeDialog(); } );
    layout->addWidget( jump );
    layout->addStretch( 1 );
}

void SettingsPanel::buildVideo( QVBoxLayout *layout )
{
    layout->addWidget( sectionLabel( qtr( "Picture" ), true ) );
    const struct { const char *var; const char *label; } vars[] = {
        { "aspect-ratio",     N_( "Aspect ratio" ) },
        { "crop",             N_( "Crop" ) },
        { "zoom",             N_( "Zoom" ) },
        { "deinterlace",      N_( "Deinterlace" ) },
        { "deinterlace-mode", N_( "Deinterlace mode" ) },
    };
    for( const auto &v : vars )
    {
        QComboBox *combo = new QComboBox;
        combo->setObjectName( "panelCombo" );
        layout->addLayout( labeledRow( qtr( v.label ), combo ) );
        const QByteArray var( v.var );
        connect( combo, static_cast<void (QComboBox::*)(int)>( &QComboBox::activated ), this,
                 [this, combo, var]( int i ) {
            vout_thread_t *p_vout = THEMIM->getVout();
            applyChoice( VLC_OBJECT( p_vout ), var.constData(), combo->itemData( i ) );
            if( p_vout )
                vlc_object_release( p_vout );
        } );
        refreshers << [this, combo, var]() {
            vout_thread_t *p_vout = THEMIM->getVout();
            fillChoices( combo, VLC_OBJECT( p_vout ), var.constData() );
            if( p_vout )
                vlc_object_release( p_vout );
        };
    }

    layout->addWidget( divider() );
    QToolButton *effects = footerButton( qtr( "Colors, crop and other filters..." ) );
    connect( effects, &QToolButton::clicked, this, [this]() { THEDP->extendedDialog(); } );
    layout->addWidget( effects );
    layout->addStretch( 1 );
}

void SettingsPanel::buildAudio( QVBoxLayout *layout )
{
    layout->addWidget( sectionLabel( qtr( "Synchronization" ), true ) );
    QLabel *delayValue = new QLabel;
    delayValue->setObjectName( "panelValue" );
    layout->addLayout( labeledRow( qtr( "Audio track delay" ), delayValue ) );
    QSlider *delay = new QSlider( Qt::Horizontal );
    delay->setObjectName( "panelSlider" );
    delay->setRange( -10000, 10000 ); /* ms */
    delay->setSingleStep( 50 );
    delay->setPageStep( 500 );
    layout->addWidget( delay );
    QHBoxLayout *scale = new QHBoxLayout;
    foreach( const QString &t, QStringList() << "-10 s" << "0 s" << "+10 s" )
    {
        QLabel *l = new QLabel( t );
        l->setObjectName( "panelScale" );
        scale->addWidget( l );
        if( t != "+10 s" )
            scale->addStretch( 1 );
    }
    layout->addLayout( scale );
    connect( delay, &QSlider::valueChanged, this, [this, delayValue]( int v ) { delayValue->setText( formatDelay( v ) ); } );
    connect( delay, &QSlider::valueChanged, this, [this]( int v ) {
        input_thread_t *p_input = THEMIM->getInput();
        if( p_input )
            var_SetInteger( p_input, "audio-delay", (int64_t)v * 1000 );
    } );
    refreshers << [this, delay, delayValue]() {
        const QSignalBlocker blocker( delay );
        input_thread_t *p_input = THEMIM->getInput();
        const int v = p_input ? (int)( var_GetInteger( p_input, "audio-delay" ) / 1000 ) : 0;
        delay->setValue( v );
        delay->setEnabled( p_input != NULL );
        delayValue->setText( formatDelay( v ) );
    };

    layout->addWidget( divider() );
    layout->addWidget( sectionLabel( qtr( "Output" ), true ) );
    QComboBox *stereo = new QComboBox;
    stereo->setObjectName( "panelCombo" );
    layout->addLayout( labeledRow( qtr( "Stereo mode" ), stereo ) );
    connect( stereo, static_cast<void (QComboBox::*)(int)>( &QComboBox::activated ), this, [this, stereo]( int i ) {
        audio_output_t *p_aout = THEMIM->getAout();
        applyChoice( VLC_OBJECT( p_aout ), "stereo-mode", stereo->itemData( i ) );
        if( p_aout )
            vlc_object_release( p_aout );
    } );
    refreshers << [this, stereo]() {
        audio_output_t *p_aout = THEMIM->getAout();
        fillChoices( stereo, VLC_OBJECT( p_aout ), "stereo-mode" );
        if( p_aout )
            vlc_object_release( p_aout );
    };

    layout->addWidget( divider() );
    layout->addWidget( sectionLabel( qtr( "Equalizer" ), true ) );
    QCheckBox *eqEnable = new QCheckBox( qtr( "Enable" ) );
    eqEnable->setObjectName( "panelCheck" );
    layout->addWidget( eqEnable );
    QComboBox *preset = new QComboBox;
    preset->setObjectName( "panelCombo" );
    for( int i = 0; i < NB_PRESETS; i++ )
        preset->addItem( qtr( preset_list_text[i] ), qfu( preset_list[i] ) );
    layout->addLayout( labeledRow( qtr( "Preset" ), preset ) );
    connect( eqEnable, &QCheckBox::toggled, this, [this, preset]( bool b ) {
        playlist_EnableAudioFilter( THEPL, "equalizer", b );
        preset->setEnabled( b );
    } );
    connect( preset, static_cast<void (QComboBox::*)(int)>( &QComboBox::activated ), this, [this, preset]( int i ) {
        const QByteArray name = preset->itemData( i ).toString().toUtf8();
        config_PutPsz( p_intf, "equalizer-preset", name.constData() );
        audio_output_t *p_aout = THEMIM->getAout();
        if( p_aout )
        {
            var_SetString( p_aout, "equalizer-preset", name.constData() );
            vlc_object_release( p_aout );
        }
    } );
    refreshers << [this, eqEnable, preset]() {
        const QSignalBlocker b1( eqEnable ), b2( preset );
        audio_output_t *p_aout = THEMIM->getAout();
        char *filters = p_aout ? var_GetNonEmptyString( p_aout, "audio-filter" )
                               : config_GetPsz( p_intf, "audio-filter" );
        const bool b_on = filters && strstr( filters, "equalizer" );
        free( filters );
        eqEnable->setChecked( b_on );
        preset->setEnabled( b_on );
        char *current = p_aout ? var_GetNonEmptyString( p_aout, "equalizer-preset" )
                               : config_GetPsz( p_intf, "equalizer-preset" );
        const int index = current ? preset->findData( qfu( current ) ) : -1;
        free( current );
        if( index >= 0 )
            preset->setCurrentIndex( index );
        if( p_aout )
            vlc_object_release( p_aout );
    };

    layout->addWidget( divider() );
    QToolButton *effects = footerButton( qtr( "Advanced audio effects..." ) );
    connect( effects, &QToolButton::clicked, this, [this]() { THEDP->extendedDialog(); } );
    layout->addWidget( effects );
    layout->addStretch( 1 );
}

void SettingsPanel::buildSubtitles( QVBoxLayout *layout )
{
    layout->addWidget( sectionLabel( qtr( "Defaults" ), true ) );
    QLineEdit *language = new QLineEdit;
    language->setObjectName( "panelEdit" );
    language->setPlaceholderText( qtr( "e.g. pt,en" ) );
    language->setToolTip( qtr( "Subtitle language selected automatically on every media" ) );
    layout->addLayout( labeledRow( qtr( "Subtitle language" ), language ) );
    connect( language, &QLineEdit::editingFinished, this, [this, language]() {
        config_PutPsz( p_intf, "sub-language", qtu( language->text().trimmed() ) );
        config_SaveConfigFile( p_intf );
    } );
    refreshers << [this, language]() {
        char *psz = config_GetPsz( p_intf, "sub-language" );
        language->setText( qfu( psz ) );
        free( psz );
    };

    layout->addWidget( divider() );
    layout->addWidget( sectionLabel( qtr( "Synchronization" ), true ) );
    QLabel *delayValue = new QLabel;
    delayValue->setObjectName( "panelValue" );
    layout->addLayout( labeledRow( qtr( "Subtitle track delay" ), delayValue ) );
    QSlider *delay = new QSlider( Qt::Horizontal );
    delay->setObjectName( "panelSlider" );
    delay->setRange( -10000, 10000 ); /* ms */
    delay->setSingleStep( 50 );
    delay->setPageStep( 500 );
    layout->addWidget( delay );
    QHBoxLayout *scale = new QHBoxLayout;
    foreach( const QString &t, QStringList() << "-10 s" << "0 s" << "+10 s" )
    {
        QLabel *l = new QLabel( t );
        l->setObjectName( "panelScale" );
        scale->addWidget( l );
        if( t != "+10 s" )
            scale->addStretch( 1 );
    }
    layout->addLayout( scale );
    connect( delay, &QSlider::valueChanged, this, [this, delayValue]( int v ) { delayValue->setText( formatDelay( v ) ); } );
    connect( delay, &QSlider::valueChanged, this, [this]( int v ) {
        input_thread_t *p_input = THEMIM->getInput();
        if( p_input )
            var_SetInteger( p_input, "spu-delay", (int64_t)v * 1000 );
    } );

    QDoubleSpinBox *fps = new QDoubleSpinBox;
    fps->setObjectName( "panelSpin" );
    fps->setRange( 0.0, 100.0 );
    fps->setSingleStep( 0.5 );
    fps->setDecimals( 3 );
    fps->setSuffix( " fps" );
    fps->setToolTip( qtr( "Speed of the subtitles (0 = original)" ) );
    layout->addLayout( labeledRow( qtr( "Subtitle speed" ), fps ) );
    connect( fps, static_cast<void (QDoubleSpinBox::*)(double)>( &QDoubleSpinBox::valueChanged ), this, [this]( double f ) {
        input_thread_t *p_input = THEMIM->getInput();
        if( p_input )
            var_SetFloat( p_input, "sub-fps", f );
    } );
    refreshers << [this, delay, delayValue, fps]() {
        const QSignalBlocker b1( delay ), b2( fps );
        input_thread_t *p_input = THEMIM->getInput();
        const int v = p_input ? (int)( var_GetInteger( p_input, "spu-delay" ) / 1000 ) : 0;
        delay->setValue( v );
        delay->setEnabled( p_input != NULL );
        delayValue->setText( formatDelay( v ) );
        fps->setValue( p_input ? var_GetFloat( p_input, "sub-fps" ) : 0.0 );
        fps->setEnabled( p_input != NULL );
    };

    layout->addWidget( divider() );
    layout->addWidget( sectionLabel( qtr( "Appearance" ), true ) );
    QLabel *sizeValue = new QLabel;
    sizeValue->setObjectName( "panelValue" );
    layout->addLayout( labeledRow( qtr( "Text size" ), sizeValue ) );
    QSlider *size = new QSlider( Qt::Horizontal );
    size->setObjectName( "panelSlider" );
    size->setRange( 25, 300 ); /* % */
    size->setSingleStep( 5 );
    size->setPageStep( 25 );
    layout->addWidget( size );
    connect( size, &QSlider::valueChanged, this, [this, sizeValue]( int v ) {
        sizeValue->setText( QString::number( v ) + " %" );
        var_SetInteger( THEPL, "sub-text-scale", v );
    } );
    refreshers << [this, size, sizeValue]() {
        const QSignalBlocker blocker( size );
        const int v = (int)var_GetInteger( THEPL, "sub-text-scale" );
        size->setValue( v );
        sizeValue->setText( QString::number( v ) + " %" );
    };

    layout->addWidget( divider() );
    QToolButton *addSub = footerButton( qtr( "Add a subtitle file..." ) );
    connect( addSub, &QToolButton::clicked, this, [this]() { THEDP->loadSubtitlesFile(); } );
    layout->addWidget( addSub );
    layout->addStretch( 1 );
}
