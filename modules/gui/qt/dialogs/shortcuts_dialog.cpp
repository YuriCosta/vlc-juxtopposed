/*****************************************************************************
 * shortcuts_dialog.cpp : keyboard shortcuts of the redesigned interface
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

#include "dialogs/shortcuts_dialog.hpp"
#include "util/customwidgets.hpp" /* qtEventToVLCKey, VLCKeyToString */
#include "util/redesign.hpp"
#include "main_interface.hpp"

#include <vlc_actions.h>
#include <vlc_configuration.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>    /* CONFIG_ITEM_KEY */

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <initializer_list>

#define FIELD_WIDTH 270
#define FIELD_GAP 89

/* One hotkey: shows the keys, a click waits for the new one */
class ShortcutField : public QAbstractButton
{
public:
    ShortcutField( QWidget *parent = NULL ) : QAbstractButton( parent ), b_capturing( false )
    {
        setFixedSize( FIELD_WIDTH, 33 );
        setCursor( Qt::PointingHandCursor );
        setFocusPolicy( Qt::StrongFocus );
        connect( this, &QAbstractButton::clicked, this, [this]() { setCapturing( true ); } );
    }

    /* the key, its config name ("Ctrl+o") and the Qt one, for the menus */
    std::function<void( unsigned, const QString & )> onKey;
    std::function<void()> onClear;
    std::function<void( bool )> onCapturing;

    void setKeys( const QString &keys )
    {
        QStringList names;
        foreach( const QString &key, keys.split( '\t', QString::SkipEmptyParts ) )
        {
            const unsigned code = vlc_str2keycode( qtu( key ) );
            names << ( code != KEY_UNSET ? VLCKeyToString( code, true ) : key );
        }
        setText( names.join( ", " ) );
        setToolTip( text() );
        update();
    }

    QSize sizeHint() const Q_DECL_OVERRIDE { return QSize( FIELD_WIDTH, 33 ); }

protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE
    {
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        p.setPen( b_capturing ? QPen( Redesign::Accent, 1 )
                : underMouse() || hasFocus() ? QPen( Redesign::DividerBright, 1 ) : Qt::NoPen );
        p.setBrush( Redesign::BgB2 );
        p.drawRoundedRect( QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 ), 6, 6 );

        /* the pencil, 241 px from the left */
        QPixmap pencil = QIcon( ":/redesign/edit.svg" ).pixmap( 24, 24 );
        if( b_capturing )
            pencil = Redesign::tinted( pencil, Redesign::Accent );
        else if( underMouse() )
            pencil = Redesign::tinted( pencil, Redesign::FgA2 );
        p.drawPixmap( QRect( width() - 29, ( height() - 24 ) / 2, 24, 24 ), pencil );

        QFont f( "Consolas" );
        f.setStyleHint( QFont::TypeWriter );
        f.setPixelSize( 14 );
        p.setFont( f );
        const QRect textRect = rect().adjusted( 32, 0, -32, 0 );
        if( b_capturing )
        {
            QFont normal = font();
            normal.setPixelSize( 13 );
            p.setFont( normal );
            p.setPen( Redesign::Accent );
            p.drawText( textRect, Qt::AlignCenter, qtr( "Press a key..." ) );
        }
        else
        {
            p.setPen( QColor( 0xC6, 0xC6, 0xC6 ) );
            p.drawText( textRect, Qt::AlignCenter,
                        p.fontMetrics().elidedText( text(), Qt::ElideRight, textRect.width() ) );
        }
    }

    bool event( QEvent *e ) Q_DECL_OVERRIDE
    {
        /* Tab and the shortcuts of the menus are keys too while waiting for one */
        if( b_capturing && e->type() == QEvent::ShortcutOverride )
        {
            e->accept();
            return true;
        }
        if( b_capturing && e->type() == QEvent::KeyPress )
        {
            keyPressEvent( static_cast<QKeyEvent *>( e ) );
            return true;
        }
        return QAbstractButton::event( e );
    }

    void keyPressEvent( QKeyEvent *e ) Q_DECL_OVERRIDE
    {
        if( !b_capturing )
        {
            QAbstractButton::keyPressEvent( e );
            return;
        }
        switch( e->key() )
        {
        case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Meta:
        case Qt::Key_Alt: case Qt::Key_AltGr:
            return;
        case Qt::Key_Escape:
            setCapturing( false );
            return;
        case Qt::Key_Backspace:
            if( e->modifiers() == Qt::NoModifier )
            {
                setCapturing( false );
                if( onClear )
                    onClear();
                return;
            }
        }
        const unsigned key = qtEventToVLCKey( e );
        const QString sequence = QKeySequence( e->key() | e->modifiers() ).toString();
        setCapturing( false );
        if( onKey )
            onKey( key, sequence );
    }

    void wheelEvent( QWheelEvent *e ) Q_DECL_OVERRIDE
    {
        if( !b_capturing )
        {
            QAbstractButton::wheelEvent( e );
            return;
        }
        const unsigned key = qtWheelEventToVLCKey( e );
        setCapturing( false );
        if( onKey )
            onKey( key, QString() );
    }

    void focusOutEvent( QFocusEvent *e ) Q_DECL_OVERRIDE
    {
        setCapturing( false );
        QAbstractButton::focusOutEvent( e );
    }

private:
    void setCapturing( bool b )
    {
        if( b == b_capturing )
            return;
        b_capturing = b;
        if( b )
            setFocus();
        update();
        if( onCapturing )
            onCapturing( b );
    }

    bool b_capturing;
};

namespace {

enum { GROUP_BASIC, GROUP_PLAYBACK, GROUP_NAVIGATION, GROUP_AUDIO, GROUP_SUBTITLES,
       GROUP_VIDEO, GROUP_OTHER, GROUP_COUNT };

enum { FIELDS_ALL, FIELDS_ACTIONS, FIELDS_HOTKEYS, FIELDS_GLOBAL };

const char *groupName( int group )
{
    switch( group )
    {
    case GROUP_BASIC:      return N_( "Basic" );
    case GROUP_PLAYBACK:   return N_( "Playback" );
    case GROUP_NAVIGATION: return N_( "Navigation" );
    case GROUP_AUDIO:      return N_( "Audio" );
    case GROUP_SUBTITLES:  return N_( "Subtitles" );
    case GROUP_VIDEO:      return N_( "Video" );
    default:               return N_( "Other" );
    }
}

/* "key-play-pause" -> GROUP_PLAYBACK */
int groupOf( const QString &name )
{
    static const QStringList basic = QStringList() << "toggle-fullscreen" << "leave-fullscreen"
        << "position" << "intf-show" << "intf-boss" << "intf-popup-menu" << "snapshot" << "quit";
    static const QStringList playback = QStringList() << "play-pause" << "pause" << "play" << "stop"
        << "next" << "prev" << "faster" << "slower" << "rate-normal" << "rate-faster-fine"
        << "rate-slower-fine" << "frame-next" << "random" << "loop" << "record"
        << "program-sid-next" << "program-sid-prev";
    const QString action = name.mid( 4 );
    if( basic.contains( action ) )
        return GROUP_BASIC;
    if( playback.contains( action ) )
        return GROUP_PLAYBACK;
    for( const char *prefix : { "jump", "title-", "chapter-", "disc-menu", "nav-" } )
        if( action.startsWith( prefix ) )
            return GROUP_NAVIGATION;
    for( const char *prefix : { "vol-", "audiodelay-", "audio-track", "audiodevice-" } )
        if( action.startsWith( prefix ) )
            return GROUP_AUDIO;
    if( action.startsWith( "sub" ) )
        return GROUP_SUBTITLES;
    for( const char *prefix : { "aspect-ratio", "crop", "uncrop", "toggle-autoscale", "incr-",
                                "decr-", "deinterlace", "zoom", "unzoom", "wallpaper", "viewpoint" } )
        if( action.startsWith( prefix ) )
            return GROUP_VIDEO;
    return GROUP_OTHER;
}

QWidget *divider()
{
    QFrame *line = new QFrame;
    line->setObjectName( "scDivider" );
    line->setFixedHeight( 1 );
    return line;
}

/* Action ....... [ hotkey ] [ global ] */
QHBoxLayout *columns( QWidget *action, QWidget *hotkey, QWidget *global )
{
    QHBoxLayout *l = new QHBoxLayout;
    l->setContentsMargins( 0, 0, 0, 0 );
    l->setSpacing( 0 );
    l->addWidget( action, 1 );
    l->addSpacing( 10 );
    l->addWidget( hotkey );
    l->addSpacing( FIELD_GAP );
    l->addWidget( global );
    return l;
}

QLabel *columnTitle( const QString &text, Qt::Alignment align )
{
    QLabel *label = new QLabel( text );
    label->setObjectName( "scColumn" );
    label->setAlignment( align | Qt::AlignVCenter );
    if( align != Qt::AlignLeft )
        label->setFixedWidth( FIELD_WIDTH );
    return label;
}

QString keysText( const QString &keys )
{
    QStringList names;
    foreach( const QString &key, keys.split( '\t', QString::SkipEmptyParts ) )
    {
        const unsigned code = vlc_str2keycode( qtu( key ) );
        names << ( code != KEY_UNSET ? VLCKeyToString( code, true ) : key );
    }
    return names.join( " " );
}

} // namespace

void ShortcutsDialog::open( intf_thread_t *p_intf, QWidget *parent )
{
    ShortcutsDialog dialog( p_intf, parent );
    dialog.exec();
}

void ShortcutsDialog::resetAll( intf_thread_t *p_intf )
{
    module_t *p_main = module_get_main();
    unsigned count;
    module_config_t *p_config = module_config_get( p_main, &count );
    for( unsigned i = 0; i < count; i++ )
    {
        const module_config_t *p_item = p_config + i;
        if( p_item->i_type != CONFIG_ITEM_KEY || !p_item->psz_name )
            continue;
        config_PutPsz( p_intf, p_item->psz_name, p_item->orig.psz );
    }
    module_config_free( p_config );
    config_SaveConfigFile( p_intf );
}

ShortcutsDialog::ShortcutsDialog( intf_thread_t *_p_intf, QWidget *parent )
               : QDialog( parent ), p_intf( _p_intf )
{
    setWindowTitle( qtr( "Keyboard Shortcuts" ) );
    setWindowFlags( windowFlags() & ~Qt::WindowContextHelpButtonHint );
    setObjectName( "shortcutsDialog" );
    Redesign::darkTitleBar( this );

    /* Figma: 1048 x 800 */
    QScreen *screen = QGuiApplication::primaryScreen();
    const int maxHeight = screen ? screen->availableGeometry().height() - 80 : 800;
    resize( 1048, qMin( 800, maxHeight ) );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    /* [🔍 Search Keyboard Shortcuts...      ] [All Fields  v] */
    QWidget *top = new QWidget;
    top->setObjectName( "scTop" );
    QHBoxLayout *topLayout = new QHBoxLayout( top );
    topLayout->setContentsMargins( 9, 9, 9, 9 );
    topLayout->setSpacing( 10 );
    searchEdit = new QLineEdit;
    searchEdit->setObjectName( "scSearch" );
    searchEdit->setPlaceholderText( qtr( "Search Keyboard Shortcuts..." ) );
    searchEdit->setClearButtonEnabled( true );
    searchEdit->addAction( QIcon( ":/redesign/lib-search.svg" ), QLineEdit::LeadingPosition );
    connect( searchEdit, &QLineEdit::textChanged, this, &ShortcutsDialog::filter );
    topLayout->addWidget( searchEdit, 1 );
    fieldBox = new QComboBox;
    fieldBox->setObjectName( "scFields" );
    fieldBox->setFixedWidth( 200 );
    fieldBox->addItem( qtr( "All Fields" ), FIELDS_ALL );
    fieldBox->addItem( qtr( "Actions" ), FIELDS_ACTIONS );
    fieldBox->addItem( qtr( "Hotkeys" ), FIELDS_HOTKEYS );
    fieldBox->addItem( qtr( "Global Hotkeys" ), FIELDS_GLOBAL );
    connect( fieldBox, QOverload<int>::of( &QComboBox::activated ), this, &ShortcutsDialog::filter );
    topLayout->addWidget( fieldBox );
    layout->addWidget( top );

    /* Action          Hotkey          Global Hotkey */
    QWidget *header = new QWidget;
    QVBoxLayout *headerLayout = new QVBoxLayout( header );
    headerLayout->setContentsMargins( 20, 20, 20, 0 );
    headerLayout->setSpacing( 20 );
    QWidget *titles = new QWidget;
    titles->setLayout( columns( columnTitle( qtr( "Action" ), Qt::AlignLeft ),
                                columnTitle( qtr( "Hotkey" ), Qt::AlignHCenter ),
                                columnTitle( qtr( "Global Hotkey" ), Qt::AlignHCenter ) ) );
    titles->setToolTip( qtr( "Global hotkeys also work when VLC is not the active window." ) );
    headerLayout->addWidget( titles );
    headerLayout->addWidget( divider() );
    layout->addWidget( header );

    /* The groups */
    QScrollArea *scroll = new QScrollArea;
    scroll->setObjectName( "scScroll" );
    scroll->setFrameShape( QFrame::NoFrame );
    scroll->setWidgetResizable( true );
    scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    QWidget *content = new QWidget;
    content->setObjectName( "scContent" );
    QVBoxLayout *contentLayout = new QVBoxLayout( content );
    contentLayout->setContentsMargins( 20, 0, 20, 0 );
    contentLayout->setSpacing( 0 );
    scroll->setWidget( content );
    layout->addWidget( scroll, 1 );

    QVBoxLayout *groupLayouts[GROUP_COUNT];
    for( int i = 0; i < GROUP_COUNT; i++ )
    {
        QWidget *line = divider();
        QWidget *group = new QWidget;
        QVBoxLayout *l = new QVBoxLayout( group );
        l->setContentsMargins( 0, 20, 0, 20 );
        l->setSpacing( 12 );
        QLabel *title = new QLabel( qtr( groupName( i ) ) );
        title->setObjectName( "scGroup" );
        l->addWidget( title );
        l->addSpacing( 8 );
        contentLayout->addWidget( line );
        contentLayout->addWidget( group );
        groups << qMakePair( group, line );
        groupLayouts[i] = l;
    }
    contentLayout->addStretch( 1 );

    load();
    for( int i = 0; i < shortcuts.count(); i++ )
    {
        Shortcut &s = shortcuts[i];
        const int group = groupOf( s.name );
        QLabel *label = new QLabel( s.text );
        label->setObjectName( "scAction" );
        label->setToolTip( s.text );
        s.row = new QWidget;
        for( int column = 0; column < 2; column++ )
        {
            ShortcutField *field = new ShortcutField;
            field->setKeys( s.keys[column] );
            field->onKey = [this, i, column]( unsigned key, const QString &sequence ) {
                capture( i, column, key );
                /* the menus of the window take their keys first */
                QWidget *mainWindow = p_intf->p_sys->p_mi;
                if( !sequence.isEmpty() && mainWindow )
                    foreach( QAction *action, mainWindow->findChildren<QAction *>() )
                        if( action->shortcuts().contains( QKeySequence( sequence ) ) )
                            noteLabel->setText( qtr( "%1 is also the shortcut of \"%2\" in the menus, which comes first." )
                                                .arg( VLCKeyToString( key, true ) )
                                                .arg( action->text().remove( '&' ) ) );
            };
            field->onClear = [this, i, column]() { clear( i, column ); };
            field->onCapturing = [this]( bool b ) {
                if( b )
                    noteLabel->setText( qtr( "Press the new key or combination. Esc cancels, Backspace removes the shortcut." ) );
                else
                    updateState();
            };
            s.fields[column] = field;
        }
        s.row->setLayout( columns( label, s.fields[0], s.fields[1] ) );
        s.group = groups.at( group ).first;
        groupLayouts[group]->addWidget( s.row );
    }

    /* [Cancel]          note          [Save] */
    QWidget *footer = new QWidget;
    footer->setObjectName( "scFooter" );
    QHBoxLayout *footerLayout = new QHBoxLayout( footer );
    footerLayout->setContentsMargins( 20, 20, 20, 20 );
    footerLayout->setSpacing( 20 );
    QToolButton *cancelButton = new QToolButton;
    cancelButton->setObjectName( "scCancel" );
    cancelButton->setText( qtr( "Cancel" ) );
    cancelButton->setCursor( Qt::PointingHandCursor );
    connect( cancelButton, &QToolButton::clicked, this, &QDialog::reject );
    footerLayout->addWidget( cancelButton );
    noteLabel = new QLabel;
    noteLabel->setObjectName( "scNote" );
    noteLabel->setAlignment( Qt::AlignCenter );
    noteLabel->setWordWrap( true );
    footerLayout->addWidget( noteLabel, 1 );
    saveButton = new QToolButton;
    saveButton->setObjectName( "scSave" );
    saveButton->setText( qtr( "Save" ) );
    saveButton->setCursor( Qt::PointingHandCursor );
    connect( saveButton, &QToolButton::clicked, this, &ShortcutsDialog::save );
    footerLayout->addWidget( saveButton );
    layout->addWidget( footer );

    filter();
    updateState();
    searchEdit->setFocus();
}

/* The actions with a name, in the order of the core */
void ShortcutsDialog::load()
{
    module_t *p_main = module_get_main();
    unsigned count;
    module_config_t *p_config = module_config_get( p_main, &count );
    for( unsigned i = 0; i < count; i++ )
    {
        const module_config_t *p_item = p_config + i;
        if( p_item->i_type != CONFIG_ITEM_KEY || !p_item->psz_name
         || strncmp( p_item->psz_name, "key-", 4 ) || EMPTY_STR( p_item->psz_text ) )
            continue;
        Shortcut s;
        s.name = qfu( p_item->psz_name );
        s.text = qtr( p_item->psz_text );
        char *psz = config_GetPsz( p_intf, p_item->psz_name );
        s.keys[0] = s.saved[0] = qfu( psz );
        free( psz );
        const QByteArray global = "global-" + s.name.toUtf8();
        if( config_FindConfig( global.constData() ) )
        {
            psz = config_GetPsz( p_intf, global.constData() );
            s.keys[1] = s.saved[1] = qfu( psz );
            free( psz );
        }
        s.fields[0] = s.fields[1] = NULL;
        s.row = s.group = NULL;
        shortcuts << s;
    }
    module_config_free( p_config );
}

/* A key belongs to one action: the others lose it */
void ShortcutsDialog::capture( int index, int column, unsigned key )
{
    if( key == KEY_UNSET )
        return;
    const QString name = VLCKeyToString( key, false );
    QStringList losers;
    for( int i = 0; i < shortcuts.count(); i++ )
    {
        if( i == index )
            continue;
        QStringList keys = shortcuts[i].keys[column].split( '\t', QString::SkipEmptyParts );
        if( keys.removeAll( name ) )
        {
            shortcuts[i].keys[column] = keys.join( '\t' );
            shortcuts[i].fields[column]->setKeys( shortcuts[i].keys[column] );
            losers << "\"" + shortcuts[i].text + "\"";
        }
    }
    shortcuts[index].keys[column] = name;
    shortcuts[index].fields[column]->setKeys( name );
    updateState();
    if( !losers.isEmpty() )
        noteLabel->setText( qtr( "%1 was removed from %2." ).arg( VLCKeyToString( key, true ) )
                                                         .arg( losers.join( ", " ) ) );
}

void ShortcutsDialog::clear( int index, int column )
{
    shortcuts[index].keys[column].clear();
    shortcuts[index].fields[column]->setKeys( QString() );
    updateState();
}

/* Only the matching actions, and the groups that still have some */
void ShortcutsDialog::filter()
{
    const QString text = searchEdit->text().trimmed();
    const int fields = fieldBox->currentData().toInt();
    foreach( const Shortcut &s, shortcuts )
    {
        QStringList haystack;
        if( fields == FIELDS_ALL || fields == FIELDS_ACTIONS )
            haystack << s.text;
        if( fields == FIELDS_ALL || fields == FIELDS_HOTKEYS )
            haystack << keysText( s.keys[0] );
        if( fields == FIELDS_ALL || fields == FIELDS_GLOBAL )
            haystack << keysText( s.keys[1] );
        s.row->setVisible( text.isEmpty() || haystack.join( '\n' ).contains( text, Qt::CaseInsensitive ) );
    }
    bool b_first = true;
    for( int i = 0; i < groups.count(); i++ )
    {
        bool b_visible = false;
        foreach( const Shortcut &s, shortcuts )
            if( s.group == groups.at( i ).first && !s.row->isHidden() )
                b_visible = true;
        groups.at( i ).first->setVisible( b_visible );
        groups.at( i ).second->setVisible( b_visible && !b_first );
        if( b_visible )
            b_first = false;
    }
}

void ShortcutsDialog::updateState()
{
    bool b_changed = false;
    foreach( const Shortcut &s, shortcuts )
        b_changed |= s.keys[0] != s.saved[0] || s.keys[1] != s.saved[1];
    saveButton->setEnabled( b_changed );
    noteLabel->setText( b_changed ? qtr( "The new shortcuts work the next time VLC starts." )
                                  : qtr( "Click a shortcut to change it." ) );
}

void ShortcutsDialog::save()
{
    foreach( const Shortcut &s, shortcuts )
    {
        if( s.keys[0] != s.saved[0] )
            config_PutPsz( p_intf, qtu( s.name ), qtu( s.keys[0] ) );
        if( s.keys[1] != s.saved[1] )
            config_PutPsz( p_intf, qtu( "global-" + s.name ), qtu( s.keys[1] ) );
    }
    config_SaveConfigFile( p_intf );
    accept();
}
