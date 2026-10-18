/*****************************************************************************
 * settings_dialog.cpp : settings of the redesigned interface
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

#include "dialogs/settings_dialog.hpp"
#include "dialogs/preferences.hpp"
#include "dialogs_provider.hpp"
#include "input_manager.hpp"
#include "recents.hpp"
#include "util/redesign.hpp"

#include <vlc_configuration.h>
#include <vlc_modules.h>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QTransform>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

#ifdef _WIN32
# include "util/registry.hpp"
/* the interface languages, from simple_preferences.cpp */
int SPrefsLanguageCount();
const char *SPrefsLanguageIso( int );
const char *SPrefsLanguageName( int );
#endif

#define CONTROL_WIDTH 320

namespace {

/* Figma "Toggle": [ON ■] / [■ OFF] */
class SettingsToggle : public QAbstractButton
{
public:
    SettingsToggle( QWidget *parent = NULL ) : QAbstractButton( parent )
    {
        setCheckable( true );
        setFixedSize( 68, 32 );
        setCursor( Qt::PointingHandCursor );
    }

    QSize sizeHint() const Q_DECL_OVERRIDE { return QSize( 68, 32 ); }

protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE
    {
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        if( !isEnabled() )
            p.setOpacity( 0.4 );
        p.setPen( Qt::NoPen );
        p.setBrush( Redesign::BgB2 );
        p.drawRoundedRect( QRectF( rect() ), 6, 6 );
        QFont f = font();
        f.setPixelSize( 12 );
        p.setFont( f );
        if( isChecked() )
        {
            const QRectF label( 3, 3, 33, 26 ), knob( 39, 3, 26, 26 );
            p.setBrush( Redesign::BgAccent );
            p.drawRoundedRect( label, 3, 3 );
            p.setBrush( QColor( 0xAB, 0x63, 0x14 ) );
            p.drawRoundedRect( knob, 3, 3 );
            p.setPen( Redesign::Accent );
            p.drawText( label, Qt::AlignCenter, qtr( "ON" ) );
        }
        else
        {
            const QRectF knob( 3, 3, 26, 26 ), label( 32, 3, 33, 26 );
            p.setBrush( Redesign::BgA4 );
            p.drawRoundedRect( label, 3, 3 );
            p.setBrush( underMouse() ? QColor( 0x5A, 0x5A, 0x5A ) : QColor( 0x4A, 0x4A, 0x4A ) );
            p.drawRoundedRect( knob, 3, 3 );
            p.setPen( Redesign::FgA4 );
            p.drawText( label, Qt::AlignCenter, qtr( "OFF" ) );
        }
    }
};

QString configTip( const char *var )
{
    module_config_t *p_item = var ? config_FindConfig( var ) : NULL;
    return p_item && p_item->psz_longtext ? qtr( p_item->psz_longtext ) : QString();
}

/* Options such as "audio-filter" or "control" hold a list of modules */
bool hasModule( intf_thread_t *p_intf, const char *var, const char *module )
{
    char *psz = config_GetPsz( p_intf, var );
    const QStringList list = qfu( psz ).split( ':', QString::SkipEmptyParts );
    free( psz );
    return list.contains( module );
}

void setModule( intf_thread_t *p_intf, const char *var, const char *module, bool b_on )
{
    char *psz = config_GetPsz( p_intf, var );
    QStringList list = qfu( psz ).split( ':', QString::SkipEmptyParts );
    free( psz );
    list.removeAll( module );
    if( b_on )
        list << module;
    config_PutPsz( p_intf, var, qtu( list.join( ':' ) ) );
}

} // namespace

/* One page: sections of rows "label (i) ........ control" */
class SettingsPage : public QScrollArea
{
public:
    typedef QList<QPair<QString, QVariant> > Items;

    SettingsPage( intf_thread_t *_p_intf, const QString &_title )
        : title( _title ), p_intf( _p_intf ), advancedBox( NULL ), currentSection( NULL )
    {
        setObjectName( "settingsPage" );
        setFrameShape( QFrame::NoFrame );
        setWidgetResizable( true );
        setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        QWidget *content = new QWidget;
        content->setObjectName( "settingsContent" );
        mainLayout = new QVBoxLayout( content );
        mainLayout->setContentsMargins( 24, 12, 24, 24 );
        mainLayout->setSpacing( 0 );
        layout = mainLayout;
        setWidget( content );
    }

    /* the rows are added at the end, the stretch keeps them at the top */
    void finish()
    {
        if( advancedBox && advancedBox->layout()->count() == 0 )
        {
            advancedButton->hide();
            advancedBox->hide();
        }
        mainLayout->addStretch( 1 );
    }

    QString title;

    void section( const QString &text )
    {
        QLabel *label = new QLabel( text );
        label->setObjectName( "setSection" );
        label->setContentsMargins( 0, layout->count() ? 20 : 10, 0, 6 );
        layout->addWidget( label );
        currentSection = label;
        sections << Section( label, advancedBox != NULL );
    }

    /* Rows after this one are hidden behind "Advanced Options" */
    void beginAdvanced()
    {
        /* "Advanced Options >" centered, the chevron turns down when open */
        advancedButton = new QPushButton;
        advancedButton->setObjectName( "setAdvanced" );
        advancedButton->setText( qtr( "Advanced Options" ) );
        advancedButton->setLayoutDirection( Qt::RightToLeft );
        advancedButton->setIconSize( QSize( 20, 20 ) );
        advancedButton->setCursor( Qt::PointingHandCursor );
        mainLayout->addSpacing( 24 );
        mainLayout->addWidget( advancedButton );
        advancedBox = new QWidget;
        QVBoxLayout *box = new QVBoxLayout( advancedBox );
        box->setContentsMargins( 0, 12, 0, 0 );
        box->setSpacing( 0 );
        advancedBox->hide();
        mainLayout->addWidget( advancedBox );
        layout = box;
        QObject::connect( advancedButton, &QPushButton::clicked, advancedButton, [this]() {
            setAdvancedVisible( !advancedBox->isVisible() );
        } );
        setAdvancedVisible( false );
    }

    void setAdvancedVisible( bool b_visible )
    {
        if( !advancedBox )
            return;
        advancedBox->setVisible( b_visible );
        const QPixmap down = QIcon( ":/redesign/chevron-down.svg" ).pixmap( 40, 40 );
        advancedButton->setIcon( b_visible ? down : down.transformed( QTransform().rotate( -90 ) ) );
    }

    /* label (i)  ..............  control */
    QWidget *row( const QString &text, QWidget *control, const QString &tip = QString(), bool b_wide = false )
    {
        QWidget *row = new QWidget;
        row->setObjectName( "setRow" );
        QHBoxLayout *l = new QHBoxLayout( row );
        l->setContentsMargins( 0, 5, 0, 5 );
        l->setSpacing( 10 );
        QLabel *label = new QLabel( text );
        label->setObjectName( "setLabel" );
        l->addWidget( label );
        if( !tip.isEmpty() )
        {
            QLabel *info = new QLabel;
            info->setPixmap( QIcon( ":/redesign/info.svg" ).pixmap( 18, 18 ) );
            info->setToolTip( "<p>" + tip.toHtmlEscaped() + "</p>" );
            l->addWidget( info );
        }
        l->addStretch( 1 );
        if( control )
        {
            if( b_wide )
                control->setFixedWidth( CONTROL_WIDTH );
            l->addWidget( control );
        }
        layout->addWidget( row );
        rows << Row( row, text + " " + tip, currentSection, advancedBox != NULL );
        return row;
    }

    /* typed rows, bound to a VLC option; options of modules that are not
     * built are skipped */
    bool exists( const char *var ) const
    {
        return var && config_FindConfig( var ) != NULL;
    }

    void toggle( const QString &text, const char *var, bool b_invert = false )
    {
        if( !exists( var ) )
            return;
        const bool b_value = config_GetInt( p_intf, var ) != 0;
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        custom( text, b_value != b_invert, [p_intf, name, b_invert]( bool b ) {
            config_PutInt( p_intf, name.c_str(), b != b_invert );
        }, configTip( var ) );
    }

    void custom( const QString &text, bool b_value, std::function<void( bool )> set,
                 const QString &tip = QString() )
    {
        SettingsToggle *button = new SettingsToggle;
        button->setChecked( b_value );
        QObject::connect( button, &QAbstractButton::toggled, button, set );
        row( text, button, tip );
    }

    /* Toggle a module in a list option ("audio-filter", "control"...) */
    void module( const QString &text, const char *var, const char *mod, const QString &tip = QString() )
    {
        if( !exists( var ) || !module_exists( mod ) )
            return;
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var ), m( mod );
        custom( text, hasModule( p_intf, var, mod ), [p_intf, name, m]( bool b ) {
            setModule( p_intf, name.c_str(), m.c_str(), b );
        }, tip );
    }

    /* The values the option offers */
    void choices( const QString &text, const char *var )
    {
        if( !exists( var ) )
            return;
        Items items;
        QVariant current;
        const int type = config_GetType( var );
        if( type == VLC_VAR_INTEGER )
        {
            int64_t *values;
            char **texts;
            ssize_t count = config_GetIntChoices( VLC_OBJECT( p_intf ), var, &values, &texts );
            for( ssize_t i = 0; i < count; i++ )
            {
                items << qMakePair( qfu( texts[i] ), QVariant( (qlonglong)values[i] ) );
                free( texts[i] );
            }
            if( count > 0 )
            {
                free( values );
                free( texts );
            }
            current = (qlonglong)config_GetInt( p_intf, var );
        }
        else if( type == VLC_VAR_STRING )
        {
            char **values, **texts;
            ssize_t count = config_GetPszChoices( VLC_OBJECT( p_intf ), var, &values, &texts );
            for( ssize_t i = 0; i < count; i++ )
            {
                items << qMakePair( qfu( texts[i] ), QVariant( qfu( values[i] ) ) );
                free( texts[i] );
                free( values[i] );
            }
            if( count > 0 )
            {
                free( values );
                free( texts );
            }
            char *psz = config_GetPsz( p_intf, var );
            current = qfu( psz );
            free( psz );
        }
        if( items.isEmpty() )
            return;
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        combo( text, items, current, [p_intf, name, type]( const QVariant &v ) {
            if( type == VLC_VAR_INTEGER )
                config_PutInt( p_intf, name.c_str(), v.toLongLong() );
            else
                config_PutPsz( p_intf, name.c_str(), qtu( v.toString() ) );
        }, configTip( var ) );
    }

    QComboBox *combo( const QString &text, const Items &items, const QVariant &current,
                      std::function<void( const QVariant & )> set, const QString &tip = QString() )
    {
        QComboBox *box = new QComboBox;
        box->setObjectName( "setCombo" );
        int index = 0;
        for( int i = 0; i < items.count(); i++ )
        {
            box->addItem( items.at( i ).first, items.at( i ).second );
            if( items.at( i ).second == current )
                index = i;
        }
        box->setCurrentIndex( index );
        QObject::connect( box, QOverload<int>::of( &QComboBox::activated ), box, [box, set]( int i ) {
            set( box->itemData( i ) );
        } );
        row( text, box, tip, true );
        return box;
    }

    void integer( const QString &text, const char *var, const QString &suffix = QString(),
                  int min = 0, int max = 1000000, int scale = 1 )
    {
        if( !exists( var ) )
            return;
        QSpinBox *spin = new QSpinBox;
        spin->setObjectName( "setSpin" );
        spin->setButtonSymbols( QAbstractSpinBox::NoButtons );
        spin->setRange( min, max );
        if( min == -1 )
            spin->setSpecialValueText( qtr( "Automatic" ) );
        spin->setSuffix( suffix );
        spin->setValue( config_GetInt( p_intf, var ) / scale );
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        QObject::connect( spin, QOverload<int>::of( &QSpinBox::valueChanged ), spin, [p_intf, name, scale]( int v ) {
            config_PutInt( p_intf, name.c_str(), (int64_t)v * scale );
        } );
        row( text, spin, configTip( var ), true );
    }

    void real( const QString &text, const char *var, int decimals, double min, double max,
               double step, const QString &suffix = QString() )
    {
        if( !exists( var ) )
            return;
        QDoubleSpinBox *spin = new QDoubleSpinBox;
        spin->setObjectName( "setSpin" );
        spin->setButtonSymbols( QAbstractSpinBox::NoButtons );
        spin->setDecimals( decimals );
        spin->setRange( min, max );
        spin->setSingleStep( step );
        spin->setSuffix( suffix );
        spin->setValue( config_GetFloat( p_intf, var ) );
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        QObject::connect( spin, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), spin, [p_intf, name]( double v ) {
            config_PutFloat( p_intf, name.c_str(), v );
        } );
        row( text, spin, configTip( var ), true );
    }

    QLineEdit *text( const QString &label, const char *var, const QString &placeholder = QString(),
                     bool b_password = false )
    {
        if( !exists( var ) )
            return NULL;
        QLineEdit *edit = new QLineEdit;
        edit->setObjectName( "setText" );
        char *psz = config_GetPsz( p_intf, var );
        edit->setText( qfu( psz ) );
        free( psz );
        edit->setPlaceholderText( placeholder );
        if( b_password )
            edit->setEchoMode( QLineEdit::Password );
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        QObject::connect( edit, &QLineEdit::editingFinished, edit, [p_intf, name, edit]() {
            config_PutPsz( p_intf, name.c_str(), qtu( edit->text() ) );
        } );
        row( label, edit, configTip( var ), true );
        return edit;
    }

    /* [path          ] [Browse] */
    void path( const QString &label, const char *var, bool b_directory )
    {
        if( !exists( var ) )
            return;
        QWidget *box = new QWidget;
        QHBoxLayout *l = new QHBoxLayout( box );
        l->setContentsMargins( 0, 0, 0, 0 );
        l->setSpacing( 10 );
        QLineEdit *edit = new QLineEdit;
        edit->setObjectName( "setText" );
        char *psz = config_GetPsz( p_intf, var );
        edit->setText( QDir::toNativeSeparators( qfu( psz ) ) );
        free( psz );
        QToolButton *browse = new QToolButton;
        browse->setObjectName( "setButton" );
        browse->setText( qtr( "Browse" ) );
        l->addWidget( edit, 1 );
        l->addWidget( browse );
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        auto save = [p_intf, name, edit]() {
            config_PutPsz( p_intf, name.c_str(), qtu( QDir::toNativeSeparators( edit->text() ) ) );
        };
        QObject::connect( edit, &QLineEdit::editingFinished, edit, save );
        QObject::connect( browse, &QToolButton::clicked, edit, [edit, b_directory, save]() {
            const QString path = b_directory
                ? QFileDialog::getExistingDirectory( edit->window(), qtr( "Select a folder" ), edit->text() )
                : QFileDialog::getSaveFileName( edit->window(), qtr( "Select a file" ), edit->text(),
                                                QString(), NULL, QFileDialog::DontConfirmOverwrite );
            if( path.isEmpty() )
                return;
            edit->setText( QDir::toNativeSeparators( path ) );
            save();
        } );
        box->setFixedWidth( 400 );
        row( label, box, configTip( var ) );
    }

    /* A slider with labels under it: ticks are (position, label) */
    QSlider *slider( const QString &text, int min, int max, int step, int value,
                     const QList<QPair<int, QString> > &ticks, std::function<void( int )> set,
                     const QString &tip = QString() )
    {
        QWidget *box = new QWidget;
        QVBoxLayout *l = new QVBoxLayout( box );
        l->setContentsMargins( 0, 0, 0, 0 );
        l->setSpacing( 6 );
        QSlider *s = new QSlider( Qt::Horizontal );
        s->setObjectName( "setSlider" );
        s->setRange( min, max );
        s->setSingleStep( step );
        s->setPageStep( step );
        s->setValue( value );
        l->addWidget( s );
        if( !ticks.isEmpty() )
        {
            /* the labels are placed under their position on the groove */
            QWidget *labels = new QWidget;
            labels->setFixedHeight( 16 );
            foreach( const auto &tick, ticks )
            {
                QLabel *label = new QLabel( tick.second, labels );
                label->setObjectName( "setTick" );
                label->setProperty( "tickPos", tick.first );
            }
            labels->installEventFilter( new TickLayout( labels, min, max ) );
            l->addWidget( labels );
        }
        QObject::connect( s, &QSlider::valueChanged, s, set );
        box->setFixedWidth( CONTROL_WIDTH );
        row( text, box, tip );
        return s;
    }

    void color( const QString &text, const char *var )
    {
        if( !exists( var ) )
            return;
        QToolButton *button = new QToolButton;
        button->setObjectName( "setColor" );
        button->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        button->setCursor( Qt::PointingHandCursor );
        auto show = [button]( int rgb ) {
            QPixmap swatch( 16, 16 );
            swatch.fill( QColor( ( rgb >> 16 ) & 0xFF, ( rgb >> 8 ) & 0xFF, rgb & 0xFF ) );
            button->setIcon( swatch );
            button->setText( QString( "#%1" ).arg( rgb, 6, 16, QChar( '0' ) ).toUpper() );
        };
        show( config_GetInt( p_intf, var ) );
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        QObject::connect( button, &QToolButton::clicked, button, [p_intf, name, button, show]() {
            const int rgb = config_GetInt( p_intf, name.c_str() );
            const QColor c = QColorDialog::getColor( QColor( ( rgb >> 16 ) & 0xFF, ( rgb >> 8 ) & 0xFF, rgb & 0xFF ),
                                                     button->window() );
            if( !c.isValid() )
                return;
            const int value = ( c.red() << 16 ) | ( c.green() << 8 ) | c.blue();
            config_PutInt( p_intf, name.c_str(), value );
            show( value );
        } );
        button->setFixedWidth( 140 );
        row( text, button, configTip( var ) );
    }

    void font( const QString &text, const char *var )
    {
        if( !exists( var ) )
            return;
        QFontComboBox *box = new QFontComboBox;
        box->setObjectName( "setCombo" );
        char *psz = config_GetPsz( p_intf, var );
        if( psz && *psz )
            box->setCurrentFont( QFont( qfu( psz ) ) );
        free( psz );
        intf_thread_t *p_intf = this->p_intf;
        std::string name( var );
        QObject::connect( box, &QFontComboBox::currentFontChanged, box, [p_intf, name]( const QFont &f ) {
            config_PutPsz( p_intf, name.c_str(), qtu( f.family() ) );
        } );
        row( text, box, configTip( var ), true );
    }

    QToolButton *button( const QString &text, const QString &buttonText, std::function<void()> action,
                         const QString &tip = QString() )
    {
        QToolButton *b = new QToolButton;
        b->setObjectName( "setButton" );
        b->setText( buttonText );
        b->setCursor( Qt::PointingHandCursor );
        QObject::connect( b, &QToolButton::clicked, b, action );
        row( text, b, tip );
        return b;
    }

    /* Search: shows the matching rows (and their section), the advanced ones
     * included; returns how many match */
    int filter( const QString &text )
    {
        int matches = 0;
        bool b_advancedMatch = false;
        foreach( const Row &r, rows )
        {
            const bool b_match = text.isEmpty() || r.text.contains( text, Qt::CaseInsensitive );
            r.widget->setVisible( b_match );
            if( b_match )
            {
                matches++;
                b_advancedMatch |= r.b_advanced;
            }
        }
        foreach( const Section &s, sections )
        {
            bool b_visible = text.isEmpty();
            foreach( const Row &r, rows )
                if( r.section == s.label && !r.widget->isHidden() )
                    b_visible = true;
            s.label->setVisible( b_visible );
        }
        if( text.isEmpty() )
            setAdvancedVisible( false );
        else
            setAdvancedVisible( b_advancedMatch );
        return matches;
    }

private:
    /* Places the tick labels under the slider groove */
    class TickLayout : public QObject
    {
    public:
        TickLayout( QObject *parent, int _min, int _max ) : QObject( parent ), min( _min ), max( _max ) {}
        bool eventFilter( QObject *obj, QEvent *event ) Q_DECL_OVERRIDE
        {
            if( event->type() == QEvent::Resize || event->type() == QEvent::Show )
            {
                QWidget *w = static_cast<QWidget *>( obj );
                const int handle = 8; /* the groove ends half a handle before the edges */
                foreach( QLabel *label, w->findChildren<QLabel *>() )
                {
                    label->adjustSize();
                    const double ratio = max > min ? double( label->property( "tickPos" ).toInt() - min ) / ( max - min ) : 0;
                    const int x = handle / 2 + qRound( ratio * ( w->width() - handle ) ) - label->width() / 2;
                    label->move( qBound( 0, x, w->width() - label->width() ), 0 );
                }
            }
            return false;
        }
    private:
        int min, max;
    };

    struct Row
    {
        Row( QWidget *w, const QString &t, QWidget *s, bool a ) : widget( w ), text( t ), section( s ), b_advanced( a ) {}
        QWidget *widget;
        QString text;
        QWidget *section;
        bool b_advanced;
    };
    struct Section
    {
        Section( QLabel *l, bool a ) : label( l ), b_advanced( a ) {}
        QLabel *label;
        bool b_advanced;
    };

    intf_thread_t *p_intf;
    QVBoxLayout *mainLayout;
    QVBoxLayout *layout;
    QWidget *advancedBox;
    QPushButton *advancedButton;
    QWidget *currentSection;
    QList<Row> rows;
    QList<Section> sections;
};

/*****************************************************************************
 * The window
 *****************************************************************************/

/* one window at a time */
static QPointer<SettingsDialog> instance;

void SettingsDialog::open( intf_thread_t *p_intf, QWidget *parent, int category )
{
    if( !instance )
        instance = new SettingsDialog( p_intf, parent );
    if( category >= 0 )
        instance->showCategory( category );
    instance->show();
    instance->raise();
    instance->activateWindow();
}

SettingsDialog::SettingsDialog( intf_thread_t *_p_intf, QWidget *parent )
              : QDialog( parent ), p_intf( _p_intf )
{
    setWindowTitle( qtr( "Settings" ) );
    setWindowFlags( windowFlags() & ~Qt::WindowContextHelpButtonHint );
    setObjectName( "settingsDialog" );
    setAttribute( Qt::WA_DeleteOnClose );
    Redesign::darkTitleBar( this );

    /* Figma: 1048 x 865, smaller screens get a shorter window */
    QScreen *screen = QGuiApplication::primaryScreen();
    const int maxHeight = screen ? screen->availableGeometry().height() - 80 : 865;
    resize( 1048, qMin( 865, maxHeight ) );

    QHBoxLayout *layout = new QHBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    /* [🔍 Search] then the categories, by group */
    QWidget *side = new QWidget;
    side->setObjectName( "settingsSide" );
    side->setFixedWidth( 216 );
    QVBoxLayout *sideLayout = new QVBoxLayout( side );
    sideLayout->setContentsMargins( 8, 8, 8, 8 );
    sideLayout->setSpacing( 8 );
    searchEdit = new QLineEdit;
    searchEdit->setObjectName( "settingsSearch" );
    searchEdit->setPlaceholderText( qtr( "Search" ) );
    searchEdit->setClearButtonEnabled( true );
    searchEdit->addAction( QIcon( ":/redesign/lib-search.svg" ), QLineEdit::LeadingPosition );
    connect( searchEdit, &QLineEdit::textChanged, this, &SettingsDialog::filter );
    sideLayout->addWidget( searchEdit );

    QScrollArea *categoryScroll = new QScrollArea;
    categoryScroll->setObjectName( "settingsCategories" );
    categoryScroll->setFrameShape( QFrame::NoFrame );
    categoryScroll->setWidgetResizable( true );
    categoryScroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    categoryList = new QWidget;
    categoryList->setObjectName( "settingsCategoryList" );
    QVBoxLayout *listLayout = new QVBoxLayout( categoryList );
    listLayout->setContentsMargins( 6, 6, 6, 6 );
    listLayout->setSpacing( 8 );
    categoryScroll->setWidget( categoryList );
    sideLayout->addWidget( categoryScroll, 1 );
    layout->addWidget( side );

    categories = new QButtonGroup( this );
    connect( categories, QOverload<int>::of( &QButtonGroup::buttonClicked ), this, &SettingsDialog::showCategory );

    /* Page title, then the page */
    QWidget *right = new QWidget;
    right->setObjectName( "settingsMain" );
    QVBoxLayout *rightLayout = new QVBoxLayout( right );
    rightLayout->setContentsMargins( 0, 0, 0, 0 );
    rightLayout->setSpacing( 0 );
    titleLabel = new QLabel;
    titleLabel->setObjectName( "settingsTitle" );
    titleLabel->setFixedHeight( 50 );
    rightLayout->addWidget( titleLabel );
    pages = new QStackedWidget;
    rightLayout->addWidget( pages, 1 );
    layout->addWidget( right, 1 );

    addGroup( qtr( "App" ) );
    addCategory( CAT_GENERAL, ":/redesign/set-general.svg", qtr( "General" ), generalPage() );
    addCategory( CAT_INTERFACE, ":/redesign/set-interface.svg", qtr( "Interface" ), interfacePage() );
    addCategory( CAT_PRIVACY, ":/redesign/set-privacy.svg", qtr( "Privacy" ), privacyPage() );
    addCategory( CAT_CONTROLS, ":/redesign/set-controls.svg", qtr( "Controls" ), controlsPage() );
    addGroup( qtr( "Media" ) );
    addCategory( CAT_PLAYBACK, ":/redesign/set-playback.svg", qtr( "Playback" ), playbackPage() );
    addCategory( CAT_VIDEO, ":/redesign/lib-videos.svg", qtr( "Video" ), videoPage() );
    addCategory( CAT_AUDIO, ":/redesign/set-audio.svg", qtr( "Audio" ), audioPage() );
    addCategory( CAT_SUBTITLES, ":/redesign/set-subtitles.svg", qtr( "Subtitles" ), subtitlesPage() );
    addCategory( CAT_LIBRARY, ":/redesign/set-library.svg", qtr( "Library" ), libraryPage() );
    addGroup( qtr( "System" ) );
    addCategory( CAT_NETWORK, ":/redesign/set-network.svg", qtr( "Network" ), networkPage() );
    addCategory( CAT_PERFORMANCE, ":/redesign/set-general.svg", qtr( "Performance" ), performancePage() );
    addCategory( CAT_RECORDING, ":/redesign/set-recording.svg", qtr( "Recording" ), recordingPage() );
    addCategory( CAT_EXTENSIONS, ":/redesign/set-extensions.svg", qtr( "Extensions" ), extensionsPage() );
    listLayout->addStretch( 1 );

    showCategory( CAT_GENERAL );
}

void SettingsDialog::addGroup( const QString &name )
{
    QLabel *label = new QLabel( name );
    label->setObjectName( "settingsGroup" );
    categoryList->layout()->addWidget( label );
}

void SettingsDialog::addCategory( int id, const QString &icon, const QString &name, SettingsPage *page )
{
    QToolButton *button = new QToolButton;
    button->setObjectName( "settingsCategory" );
    button->setText( name );
    button->setIcon( Redesign::activeIcon( QIcon( icon ), QSize( 24, 24 ) ) );
    button->setIconSize( QSize( 24, 24 ) );
    button->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    button->setCheckable( true );
    button->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    button->setCursor( Qt::PointingHandCursor );
    categories->addButton( button, id );
    categoryList->layout()->addWidget( button );
    page->finish();
    pages->addWidget( page );
    pageList << page;
}

void SettingsDialog::showCategory( int id )
{
    if( id < 0 || id >= pageList.count() )
        return;
    categories->button( id )->setChecked( true );
    pages->setCurrentIndex( id );
    pageList.at( id )->verticalScrollBar()->setValue( 0 );
    titleLabel->setText( pageList.at( id )->title );
}

/* Only the categories with matching options stay, the first one is shown */
void SettingsDialog::filter( const QString &text )
{
    int first = -1;
    for( int i = 0; i < pageList.count(); i++ )
    {
        const bool b_match = pageList.at( i )->filter( text.trimmed() ) > 0;
        categories->button( i )->setVisible( b_match || text.trimmed().isEmpty() );
        if( b_match && first < 0 )
            first = i;
    }
    const int current = pages->currentIndex();
    if( !text.trimmed().isEmpty() && first >= 0 && !categories->button( current )->isVisible() )
        showCategory( first );
}

void SettingsDialog::done( int r )
{
    config_SaveConfigFile( p_intf );
    QDialog::done( r );
}

/*****************************************************************************
 * Pages
 *****************************************************************************/

SettingsPage *SettingsDialog::generalPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Global Settings" ) );

    page->section( qtr( "Appearance" ) );
#ifdef _WIN32
    {
        /* like the classic preferences: stored in the registry, used at the next start */
        SettingsPage::Items items;
        for( int i = 0; i < SPrefsLanguageCount(); i++ )
            items << qMakePair( i == 0 ? qtr( SPrefsLanguageName( i ) ) : qfu( SPrefsLanguageName( i ) ),
                                QVariant( SPrefsLanguageIso( i ) ) );
        QVLCRegistry reg( HKEY_CURRENT_USER );
        char *psz = reg.ReadRegistry( "Software\\VideoLAN\\VLC\\", "Lang", "auto" );
        const QString current = qfu( psz );
        free( psz );
        page->combo( qtr( "Interface language" ), items, current, []( const QVariant &v ) {
            QVLCRegistry reg( HKEY_CURRENT_USER );
            if( v.toString() == "auto" )
            {
                reg.DeleteValue( "Software\\VideoLAN\\VLC\\", "Lang" );
                reg.DeleteKey( "Software\\VideoLAN\\", "VLC" );
                reg.DeleteKey( "Software\\", "VideoLAN" );
            }
            else
                reg.WriteRegistry( "Software\\VideoLAN\\VLC\\", "Lang", qtu( v.toString() ) );
        }, qtr( "Used the next time VLC starts." ) );
    }
#endif
    page->toggle( qtr( "Dark interface" ), "qt-dark-palette" );

    page->section( qtr( "Startup" ) );
    page->toggle( qtr( "Show confirmation and error dialogs" ), "qt-error-dialogs" );
#ifdef UPDATE_CHECK
    page->toggle( qtr( "Check for updates automatically" ), "qt-updates-notif" );
#endif
    page->toggle( qtr( "Ask for network access at the first start" ), "qt-privacy-ask" );

    page->section( qtr( "Diagnostics" ) );
    page->toggle( qtr( "Save a log" ), "file-logging" );
    page->path( qtr( "Log file" ), "logfile", false );
    if( page->exists( "log-verbose" ) )
    {
        SettingsPage::Items items;
        items << qMakePair( qtr( "Default" ), QVariant( -1 ) )
              << qMakePair( qtr( "Errors only" ), QVariant( 0 ) )
              << qMakePair( qtr( "Errors and warnings" ), QVariant( 1 ) )
              << qMakePair( qtr( "Everything (debug)" ), QVariant( 2 ) );
        intf_thread_t *p_intf = this->p_intf;
        page->combo( qtr( "Log details" ), items, (int)config_GetInt( p_intf, "log-verbose" ),
                     [p_intf]( const QVariant &v ) { config_PutInt( p_intf, "log-verbose", v.toInt() ); },
                     configTip( "log-verbose" ) );
    }

    page->beginAdvanced();
    {
        intf_thread_t *p_intf = this->p_intf;
        page->custom( qtr( "Show minor warnings" ), config_GetInt( p_intf, "verbose" ) > 0,
                      [p_intf]( bool b ) { config_PutInt( p_intf, "verbose", b ? 1 : 0 ); },
                      configTip( "verbose" ) );
    }
    page->choices( qtr( "Log format" ), "logmode" );
    page->toggle( qtr( "Collect playback statistics" ), "stats" );
    page->button( qtr( "Open configurations folder" ), qtr( "Open" ), []() {
        char *psz = config_GetUserDir( VLC_CONFIG_DIR );
        QDesktopServices::openUrl( QUrl::fromLocalFile( qfu( psz ) ) );
        free( psz );
    } );
    page->button( qtr( "Every option of VLC" ), qtr( "Open" ), [this]() {
        PrefsDialog *prefs = new PrefsDialog( parentWidget(), p_intf );
        prefs->toggleVisible();
    }, qtr( "The complete list of options, for the modules too." ) );
    page->button( qtr( "Reset all settings" ), qtr( "Reset" ), [this]() {
        if( QMessageBox::question( this, qtr( "Reset all settings" ),
                qtr( "Every setting will go back to its default value. Continue?" ) ) != QMessageBox::Yes )
            return;
        config_ResetAll( p_intf );
        config_SaveConfigFile( p_intf );
        /* the pages show the values they were built with */
        QWidget *parent = parentWidget();
        intf_thread_t *p_intf = this->p_intf;
        instance = NULL; /* this one is deleted once closed */
        close();
        SettingsDialog::open( p_intf, parent, CAT_GENERAL );
    } );
    return page;
}

SettingsPage *SettingsDialog::interfacePage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Interface Settings" ) );
    intf_thread_t *p_intf = this->p_intf;

    page->section( qtr( "Window" ) );
    page->toggle( qtr( "Play video inside the main window" ), "embedded-video" );
    page->toggle( qtr( "Resize the window to fit the video" ), "qt-video-autoresize" );
    page->toggle( qtr( "Keep the window above other windows" ), "video-on-top" );
    page->toggle( qtr( "Show window decorations" ), "video-deco" );
    page->toggle( qtr( "Show the media name in the title bar" ), "qt-name-in-title" );
    if( page->exists( "qt-fullscreen-screennumber" ) )
    {
        SettingsPage::Items items;
        items << qMakePair( qtr( "Current screen" ), QVariant( -1 ) );
        const QList<QScreen *> screens = QGuiApplication::screens();
        for( int i = 0; i < screens.count(); i++ )
            items << qMakePair( qtr( "Screen %1 (%2)" ).arg( i + 1 ).arg( screens.at( i )->name() ), QVariant( i ) );
        page->combo( qtr( "Open fullscreen on" ), items, (int)config_GetInt( p_intf, "qt-fullscreen-screennumber" ),
                     [p_intf]( const QVariant &v ) { config_PutInt( p_intf, "qt-fullscreen-screennumber", v.toInt() ); },
                     configTip( "qt-fullscreen-screennumber" ) );
    }

    page->section( qtr( "Playback Controls" ) );
    page->toggle( qtr( "Start in compact mode" ), "qt-minimal-view" );
    page->toggle( qtr( "Show controls in fullscreen" ), "qt-fs-controller" );
    if( page->exists( "mouse-hide-timeout" ) )
        page->slider( qtr( "Hide the mouse pointer after" ), 0, 10, 1,
                      config_GetInt( p_intf, "mouse-hide-timeout" ) / 1000,
                      QList<QPair<int, QString> >() << qMakePair( 0, QString( "0 s" ) )
                          << qMakePair( 5, QString( "5 s" ) ) << qMakePair( 10, QString( "10 s" ) ),
                      [p_intf]( int v ) { config_PutInt( p_intf, "mouse-hide-timeout", v * 1000 ); },
                      configTip( "mouse-hide-timeout" ) );

    page->section( qtr( "Notifications and System Tray" ) );
    page->toggle( qtr( "Show a system tray icon" ), "qt-system-tray" );
    page->toggle( qtr( "Start minimized to the tray" ), "qt-start-minimized" );
    page->choices( qtr( "Show a popup when the track changes" ), "qt-notification" );

    page->section( qtr( "Playlist" ) );
    page->toggle( qtr( "Show the playlist as a tree" ), "playlist-tree" );
    page->custom( qtr( "Play the next file of the folder when a media ends" ),
                  MainInputManager::autoNextInFolder( p_intf ),
                  [p_intf]( bool b ) { MainInputManager::setAutoNextInFolder( p_intf, b ); },
                  qtr( "When the queue only holds the media that was opened." ) );

    page->beginAdvanced();
    if( page->exists( "qt-opacity" ) )
        page->slider( qtr( "Window opacity" ), 10, 100, 5, qRound( config_GetFloat( p_intf, "qt-opacity" ) * 100 ),
                      QList<QPair<int, QString> >() << qMakePair( 10, QString( "10%" ) ) << qMakePair( 100, QString( "100%" ) ),
                      [p_intf]( int v ) { config_PutFloat( p_intf, "qt-opacity", v / 100.f ); },
                      configTip( "qt-opacity" ) );
    page->toggle( qtr( "Pause the video when the window is minimized" ), "qt-pause-minimized" );
    page->choices( qtr( "Bring VLC to the front when playback starts" ), "qt-auto-raise" );
    page->toggle( qtr( "Show cover art in the window when playing audio" ), "qt-bgcone" );
    page->toggle( qtr( "Allow the app icon to change" ), "qt-icon-change" );
    if( page->exists( "qt-fs-opacity" ) )
        page->slider( qtr( "Fullscreen controls opacity" ), 10, 100, 5, qRound( config_GetFloat( p_intf, "qt-fs-opacity" ) * 100 ),
                      QList<QPair<int, QString> >() << qMakePair( 10, QString( "10%" ) ) << qMakePair( 100, QString( "100%" ) ),
                      [p_intf]( int v ) { config_PutFloat( p_intf, "qt-fs-opacity", v / 100.f ); },
                      configTip( "qt-fs-opacity" ) );
    if( page->exists( "qt-fs-sensitivity" ) )
    {
        /* the distance the mouse has to move to show the controls */
        SettingsPage::Items items;
        items << qMakePair( qtr( "Low" ), QVariant( 12 ) ) << qMakePair( qtr( "Medium" ), QVariant( 3 ) )
              << qMakePair( qtr( "High" ), QVariant( 1 ) );
        const int value = config_GetInt( p_intf, "qt-fs-sensitivity" );
        page->combo( qtr( "Fullscreen controls sensitivity" ), items,
                     value >= 8 ? 12 : value >= 2 ? 3 : 1,
                     [p_intf]( const QVariant &v ) { config_PutInt( p_intf, "qt-fs-sensitivity", v.toInt() ); },
                     configTip( "qt-fs-sensitivity" ) );
    }
    return page;
}

SettingsPage *SettingsDialog::privacyPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Privacy Settings" ) );
    intf_thread_t *p_intf = this->p_intf;

    page->section( qtr( "History" ) );
    page->toggle( qtr( "Save recently played items" ), "qt-recentplay" );
    page->text( qtr( "Never remember items matching" ), "qt-recentplay-filter", qtr( "e.g., private|secret" ) );
    page->button( qtr( "Clear history now" ), qtr( "Clear" ), [p_intf]() {
        RecentsMRL::getInstance( p_intf )->clear();
    }, qtr( "Forgets the recent media and where they were left off." ) );
    page->toggle( qtr( "Fetch titles and cover art from the internet" ), "metadata-network-access" );

    page->section( qtr( "Last.fm" ) );
    page->module( qtr( "Submit played tracks to Last.fm" ), "control", "audioscrobbler",
                  qtr( "Used the next time VLC starts." ) );
    page->text( qtr( "Last.fm username" ), "lastfm-username" );
    page->text( qtr( "Last.fm password" ), "lastfm-password", QString(), true );
    page->beginAdvanced();
    page->text( qtr( "Last.fm submission URL" ), "scrobbler-url" );
    return page;
}

SettingsPage *SettingsDialog::controlsPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Mouse, Keyboard & Remote Controls" ) );

    page->section( qtr( "Keyboard Shortcuts" ) );
    page->button( qtr( "Edit keyboard shortcuts" ), qtr( "Edit" ), [this]() {
        PrefsDialog *prefs = new PrefsDialog( parentWidget(), p_intf );
        prefs->toggleVisible();
    }, qtr( "Opens the classic preferences: shortcuts are in \"Hotkeys\"." ) );

    page->section( qtr( "Skip Amounts" ) );
    page->integer( qtr( "Very short skip" ), "extrashort-jump-size", " s", 1, 3600 );
    page->integer( qtr( "Short skip" ), "short-jump-size", " s", 1, 3600 );
    page->integer( qtr( "Medium skip" ), "medium-jump-size", " s", 1, 3600 );
    page->integer( qtr( "Long skip" ), "long-jump-size", " s", 1, 3600 );

    page->section( qtr( "Mouse" ) );
    page->choices( qtr( "Scroll wheel" ), "hotkeys-y-wheel-mode" );
    page->choices( qtr( "Horizontal scroll" ), "hotkeys-x-wheel-mode" );
    page->toggle( qtr( "Use the mouse on the video (DVD menus...)" ), "mouse-events" );

    page->section( qtr( "Remotes" ) );
    page->module( qtr( "Web remote control" ), "extraintf", "http",
                  qtr( "Control VLC from a browser. Used the next time VLC starts." ) );
    page->integer( qtr( "Port" ), "http-port", QString(), 1, 65535 );
    page->text( qtr( "Password" ), "http-password", QString(), true );
    page->beginAdvanced();
    page->toggle( qtr( "Disable the global volume keys" ), "qt-disable-volume-keys" );
    return page;
}

SettingsPage *SettingsDialog::playbackPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Playback Preferences" ) );
    intf_thread_t *p_intf = this->p_intf;

    page->section( qtr( "Startup" ) );
    page->choices( qtr( "Continue where you left off" ), "qt-continue" );
    page->toggle( qtr( "Start playing as soon as a file opens" ), "start-paused", true );
    if( page->exists( "rate" ) )
    {
        static const float rates[] = { 0.25f, 0.5f, 0.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f };
        QList<QPair<int, QString> > ticks;
        int current = 3;
        const float rate = config_GetFloat( p_intf, "rate" );
        for( int i = 0; i < 8; i++ )
        {
            ticks << qMakePair( i, QString::number( rates[i] ) );
            if( qAbs( rates[i] - rate ) < 0.01f )
                current = i;
        }
        page->slider( qtr( "Default playback speed" ), 0, 7, 1, current, ticks,
                      [p_intf]( int i ) { config_PutFloat( p_intf, "rate", rates[i] ); }, configTip( "rate" ) );
    }

    page->section( qtr( "Tracks" ) );
    page->choices( qtr( "Preferred video quality" ), "preferred-resolution" );
    page->text( qtr( "Preferred menu language" ), "menu-language", qtr( "e.g., en, pt" ) );
    page->integer( qtr( "Default audio track" ), "audio-track", QString(), -1, 99 );
    page->integer( qtr( "Default subtitle track" ), "sub-track", QString(), -1, 99 );

    page->section( qtr( "Order" ) );
    {
        SettingsPage::Items items;
        items << qMakePair( qtr( "Off" ), QVariant( 0 ) ) << qMakePair( qtr( "Repeat all" ), QVariant( 1 ) )
              << qMakePair( qtr( "Repeat one" ), QVariant( 2 ) );
        const int current = config_GetInt( p_intf, "repeat" ) ? 2 : config_GetInt( p_intf, "loop" ) ? 1 : 0;
        page->combo( qtr( "Repeat" ), items, current, [p_intf]( const QVariant &v ) {
            config_PutInt( p_intf, "loop", v.toInt() == 1 );
            config_PutInt( p_intf, "repeat", v.toInt() == 2 );
        } );
    }
    page->toggle( qtr( "Shuffle" ), "random" );
    page->toggle( qtr( "Stop after each item" ), "play-and-stop" );
    {
        SettingsPage::Items items;
        items << qMakePair( qtr( "Stop" ), QVariant( false ) ) << qMakePair( qtr( "Quit VLC" ), QVariant( true ) );
        page->combo( qtr( "When playback finishes" ), items, config_GetInt( p_intf, "play-and-exit" ) != 0,
                     [p_intf]( const QVariant &v ) { config_PutInt( p_intf, "play-and-exit", v.toBool() ); },
                     configTip( "play-and-exit" ) );
    }

    page->section( qtr( "Seeking" ) );
    page->toggle( qtr( "Seek precisely" ), "input-fast-seek", true );
    page->custom( qtr( "Show a preview while scrubbing" ),
                  getSettings()->value( "Redesign/SeekPreview", true ).toBool(),
                  [p_intf]( bool b ) { getSettings()->setValue( "Redesign/SeekPreview", b ); },
                  qtr( "A frame of the video over the seek bar, at the time under the mouse." ) );

    page->section( qtr( "Files & Discs" ) );
    page->text( qtr( "Default optical drive" ), "dvd", qtr( "e.g., D:" ) );
    page->choices( qtr( "Repair damaged AVI files" ), "avi-index" );
    page->toggle( qtr( "Load matching .mkv files from the same folder" ), "mkv-preload-local-dir" );

    page->beginAdvanced();
    page->path( qtr( "Time-shift buffer folder" ), "input-timeshift-path", true );
    page->integer( qtr( "Time-shift buffer size" ), "input-timeshift-granularity", " MiB", -1, 10240, 1024 * 1024 );
    return page;
}

SettingsPage *SettingsDialog::videoPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Video Defaults" ) );
    intf_thread_t *p_intf = this->p_intf;

    page->section( qtr( "Output" ) );
    page->toggle( qtr( "Enable video" ), "video" );
    page->toggle( qtr( "Start videos in fullscreen" ), "fullscreen" );
    page->toggle( qtr( "Stop the screensaver during playback" ), "disable-screensaver" );
    page->toggle( qtr( "Play video as desktop wallpaper" ), "video-wallpaper" );

    page->section( qtr( "Deinterlacing" ) );
    page->choices( qtr( "Deinterlacing" ), "deinterlace" );
    page->choices( qtr( "Method" ), "deinterlace-mode" );

    page->section( qtr( "Framing" ) );
    {
        /* the common ratios, the option also takes any "w:h" */
        auto ratioCombo = [page, p_intf]( const QString &label, const char *var ) {
            if( !page->exists( var ) )
                return;
            SettingsPage::Items items;
            items << qMakePair( qtr( "Automatic" ), QVariant( QString() ) );
            foreach( const QString &r, QStringList() << "16:9" << "4:3" << "1:1" << "16:10" << "2.21:1"
                                                     << "2.35:1" << "2.39:1" << "5:4" )
                items << qMakePair( r, QVariant( r ) );
            char *psz = config_GetPsz( p_intf, var );
            const QString current = qfu( psz );
            free( psz );
            if( !current.isEmpty() && !QStringList( { "16:9", "4:3", "1:1", "16:10", "2.21:1", "2.35:1", "2.39:1", "5:4" } ).contains( current ) )
                items << qMakePair( current, QVariant( current ) );
            std::string name( var );
            page->combo( label, items, current, [p_intf, name]( const QVariant &v ) {
                config_PutPsz( p_intf, name.c_str(), qtu( v.toString() ) );
            }, configTip( var ) );
        };
        ratioCombo( qtr( "Aspect ratio" ), "aspect-ratio" );
        ratioCombo( qtr( "Crop" ), "crop" );
    }
    page->toggle( qtr( "Scale the video to the window" ), "autoscale" );
    page->choices( qtr( "Position in the window" ), "align" );

    page->section( qtr( "Snapshots" ) );
    page->path( qtr( "Save to" ), "snapshot-path", true );
    page->text( qtr( "File name prefix" ), "snapshot-prefix" );
    page->choices( qtr( "Format" ), "snapshot-format" );
    page->toggle( qtr( "Number files sequentially" ), "snapshot-sequential" );
    page->toggle( qtr( "Show a preview after capture" ), "snapshot-preview" );

    page->beginAdvanced();
    page->text( qtr( "Custom aspect ratios" ), "custom-aspect-ratios", qtr( "e.g., 16:9,4:3" ) );
    page->text( qtr( "Custom crop ratios" ), "custom-crop-ratios", qtr( "e.g., 16:9,4:3" ) );
    page->text( qtr( "Monitor pixel aspect ratio" ), "monitor-par", qtr( "e.g., 1:1" ) );
    page->choices( qtr( "Video output" ), "vout" );
    return page;
}

SettingsPage *SettingsDialog::audioPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Audio Defaults" ) );
    intf_thread_t *p_intf = this->p_intf;

    page->section( qtr( "Output" ) );
    page->toggle( qtr( "Enable audio" ), "audio" );
    page->choices( qtr( "Output module" ), "aout" );

    page->section( qtr( "Volume" ) );
    if( page->exists( "qt-max-volume" ) )
        page->slider( qtr( "Maximum volume" ), 100, 300, 25, config_GetInt( p_intf, "qt-max-volume" ),
                      QList<QPair<int, QString> >() << qMakePair( 100, QString( "100%" ) )
                          << qMakePair( 200, QString( "200%" ) ) << qMakePair( 300, QString( "300%" ) ),
                      [p_intf]( int v ) { config_PutInt( p_intf, "qt-max-volume", v ); },
                      configTip( "qt-max-volume" ) );
    page->toggle( qtr( "Remember the volume" ), "volume-save" );
    page->real( qtr( "Volume step" ), "volume-step", 0, 1, 256, 1 );

    page->section( qtr( "Loudness" ) );
    page->module( qtr( "Normalize loudness" ), "audio-filter", "normvol", configTip( "norm-max-level" ) );
    page->real( qtr( "Target level" ), "norm-max-level", 1, 0, 10, 0.5 );
    page->choices( qtr( "Replay gain" ), "audio-replay-gain-mode" );
    page->real( qtr( "Replay gain preamp" ), "audio-replay-gain-preamp", 1, -20, 20, 0.5, " dB" );

    page->section( qtr( "Effects" ) );
    page->module( qtr( "Equalizer" ), "audio-filter", "equalizer" );
    page->choices( qtr( "Equalizer preset" ), "equalizer-preset" );
    page->toggle( qtr( "Two-pass" ), "equalizer-2pass" );
    page->module( qtr( "Spatial audio" ), "audio-filter", "spatializer" );
    page->module( qtr( "Headphone virtualization" ), "audio-filter", "headphone" );
    page->module( qtr( "Stereo widening" ), "audio-filter", "stereo_widen" );
    page->module( qtr( "Compressor" ), "audio-filter", "compressor" );
    page->module( qtr( "Chorus / flanger" ), "audio-filter", "chorus_flanger" );
    page->module( qtr( "Karaoke" ), "audio-filter", "karaoke" );

    page->section( qtr( "Tracks" ) );
    page->text( qtr( "Preferred audio language" ), "audio-language", qtr( "e.g., en, pt" ) );
    page->integer( qtr( "Audio delay" ), "audio-desync", " ms", -100000, 100000 );

    page->section( qtr( "Visualization" ) );
    page->choices( qtr( "Visualizer" ), "audio-visual" );

    page->beginAdvanced();
    page->toggle( qtr( "Pass compressed audio to the receiver (S/PDIF)" ), "spdif" );
    page->choices( qtr( "Detect Dolby Surround" ), "force-dolby-surround" );
    page->choices( qtr( "Media role" ), "role" );
    return page;
}

SettingsPage *SettingsDialog::subtitlesPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Subtitle Defaults" ) );
    intf_thread_t *p_intf = this->p_intf;

    page->section( qtr( "Tracks & Languages" ) );
    page->toggle( qtr( "Enable subtitles" ), "spu" );
    page->text( qtr( "Preferred language" ), "sub-language", qtr( "e.g., en, pt" ) );
    page->choices( qtr( "Text encoding" ), "subsdec-encoding" );

    page->section( qtr( "Loading Subtitles" ) );
    page->toggle( qtr( "Load subtitle files automatically" ), "sub-autodetect-file" );
    page->choices( qtr( "File name matching" ), "sub-autodetect-fuzzy" );

    page->section( qtr( "Appearance" ) );
    page->font( qtr( "Font" ), "freetype-font" );
    page->choices( qtr( "Font size" ), "freetype-rel-fontsize" );
    page->toggle( qtr( "Bold" ), "freetype-bold" );
    page->color( qtr( "Text color" ), "freetype-color" );
    if( page->exists( "freetype-opacity" ) )
        page->slider( qtr( "Text opacity" ), 0, 255, 5, config_GetInt( p_intf, "freetype-opacity" ),
                      QList<QPair<int, QString> >() << qMakePair( 0, QString( "0%" ) )
                          << qMakePair( 128, QString( "50%" ) ) << qMakePair( 255, QString( "100%" ) ),
                      [p_intf]( int v ) { config_PutInt( p_intf, "freetype-opacity", v ); } );
    page->choices( qtr( "Outline thickness" ), "freetype-outline-thickness" );
    page->color( qtr( "Outline color" ), "freetype-outline-color" );
    if( page->exists( "freetype-shadow-opacity" ) )
        page->custom( qtr( "Shadow" ), config_GetInt( p_intf, "freetype-shadow-opacity" ) > 0,
                      [p_intf]( bool b ) { config_PutInt( p_intf, "freetype-shadow-opacity", b ? 128 : 0 ); } );
    page->color( qtr( "Shadow color" ), "freetype-shadow-color" );
    if( page->exists( "freetype-background-opacity" ) )
        page->slider( qtr( "Background opacity" ), 0, 255, 5, config_GetInt( p_intf, "freetype-background-opacity" ),
                      QList<QPair<int, QString> >() << qMakePair( 0, QString( "0%" ) )
                          << qMakePair( 128, QString( "50%" ) ) << qMakePair( 255, QString( "100%" ) ),
                      [p_intf]( int v ) { config_PutInt( p_intf, "freetype-background-opacity", v ); } );
    page->color( qtr( "Background color" ), "freetype-background-color" );
    page->integer( qtr( "Distance from the bottom" ), "sub-margin", " px", -1000, 1000 );

    page->section( qtr( "On-Screen Messages" ) );
    page->toggle( qtr( "Show feedback for shortcuts" ), "osd" );
    page->toggle( qtr( "Show the file name when a video starts" ), "video-title-show" );
    if( page->exists( "video-title-timeout" ) )
        page->slider( qtr( "Show the file name for" ), 1, 10, 1, config_GetInt( p_intf, "video-title-timeout" ) / 1000,
                      QList<QPair<int, QString> >() << qMakePair( 1, QString( "1 s" ) ) << qMakePair( 10, QString( "10 s" ) ),
                      [p_intf]( int v ) { config_PutInt( p_intf, "video-title-timeout", v * 1000 ); } );

    page->beginAdvanced();
    page->text( qtr( "Load subtitles from these folders" ), "sub-autodetect-path" );
    page->real( qtr( "Shadow angle" ), "freetype-shadow-angle", 0, -360, 360, 15, QString::fromUtf8( "°" ) );
    page->real( qtr( "Shadow distance" ), "freetype-shadow-distance", 2, 0, 1, 0.01 );
    page->font( qtr( "Monospace font" ), "freetype-monofont" );
    page->integer( qtr( "Text scale" ), "sub-text-scale", " %", 10, 500 );
    return page;
}

SettingsPage *SettingsDialog::libraryPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Playlists, Folders & Files" ) );

    page->section( qtr( "Opening Files" ) );
    page->toggle( qtr( "Only allow one VLC window" ), "one-instance" );
    page->toggle( qtr( "Apply to files opened from the file manager" ), "one-instance-when-started-from-file" );
    page->toggle( qtr( "Add to the queue instead of replacing it" ), "playlist-enqueue" );
    page->choices( qtr( "When opening a folder" ), "recursive" );
    page->toggle( qtr( "Show hidden files" ), "show-hiddenfiles" );

    page->section( qtr( "Metadata & Artwork" ) );
    page->toggle( qtr( "Read titles and artwork from the files" ), "auto-preparse" );
    page->toggle( qtr( "Use the media library" ), "media-library" );

    page->beginAdvanced();
    page->text( qtr( "Ignored extensions" ), "ignore-filetypes" );
    return page;
}

SettingsPage *SettingsDialog::networkPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Network Connections & Security" ) );

    page->section( qtr( "Buffering" ) );
    page->integer( qtr( "Local files" ), "file-caching", " ms", 0, 60000 );
    page->integer( qtr( "Discs" ), "disc-caching", " ms", 0, 60000 );
    page->integer( qtr( "Network streams" ), "network-caching", " ms", 0, 60000 );
    page->integer( qtr( "Live capture" ), "live-caching", " ms", 0, 60000 );

    page->section( qtr( "Proxy" ) );
    page->text( qtr( "HTTP proxy" ), "http-proxy", qtr( "System proxy" ) );
    page->text( qtr( "HTTP proxy password" ), "http-proxy-pwd", QString(), true );

    page->section( qtr( "Security" ) );
    page->toggle( qtr( "Verify certificates against the system trust store" ), "gnutls-system-trust" );
    page->choices( qtr( "Cipher preference" ), "gnutls-priorities" );

    page->beginAdvanced();
    page->toggle( qtr( "Ask for network access at the first start" ), "qt-privacy-ask" );
    return page;
}

SettingsPage *SettingsDialog::performancePage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Performance Settings" ) );

    page->section( qtr( "Decoding" ) );
    page->choices( qtr( "Hardware-accelerated decoding" ), "avcodec-hw" );
    page->integer( qtr( "Video post-processing" ), "postproc-q", QString(), 0, 6 );
    page->choices( qtr( "Skip the H.264 loop filter" ), "avcodec-skiploopfilter" );
    page->integer( qtr( "Decoding threads" ), "avcodec-threads", QString(), 0, 64 );

    page->section( qtr( "Smooth Playback" ) );
    page->toggle( qtr( "Drop frames when the system can't keep up" ), "drop-late-frames" );
    page->toggle( qtr( "Skip frames" ), "skip-frames" );
    page->toggle( qtr( "Give VLC real-time priority" ), "high-priority" );

    page->beginAdvanced();
    page->choices( qtr( "Clock synchronisation" ), "clock-synchro" );
    page->integer( qtr( "Clock jitter" ), "clock-jitter", " ms", 0, 60000 );
    return page;
}

SettingsPage *SettingsDialog::recordingPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Recording, Casting & Streaming Out" ) );

    page->section( qtr( "Recording" ) );
    page->path( qtr( "Save recordings to" ), "input-record-path", true );

    page->section( qtr( "Streaming" ) );
    page->toggle( qtr( "Play locally while streaming" ), "sout-display" );
    page->toggle( qtr( "Include audio" ), "sout-audio" );
    page->toggle( qtr( "Include video" ), "sout-video" );
    page->toggle( qtr( "Include subtitles" ), "sout-spu" );

    page->beginAdvanced();
    page->choices( qtr( "Encoding preset (x264)" ), "sout-x264-preset" );
    page->choices( qtr( "Tuning (x264)" ), "sout-x264-tune" );
    return page;
}

SettingsPage *SettingsDialog::extensionsPage()
{
    SettingsPage *page = new SettingsPage( p_intf, qtr( "Add-ons & Scripts" ) );

    page->section( qtr( "Add-ons" ) );
    page->button( qtr( "Plugins and extensions" ), qtr( "Manage" ), [this]() {
        THEDP->pluginDialog();
    }, qtr( "Activate, find and remove extensions." ) );
    page->toggle( qtr( "Load add-ons on startup" ), "qt-autoload-extensions" );
    page->beginAdvanced();
    return page;
}
