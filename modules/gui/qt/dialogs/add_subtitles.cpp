/*****************************************************************************
 * add_subtitles.cpp : subtitles for a file, a folder or the queue
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

#include "dialogs/add_subtitles.hpp"
#include "input_manager.hpp"
#include "components/playlist/playlist_model.hpp"
#include "util/redesign.hpp"

#include <vlc_input.h>
#include <vlc_interface.h> /* EXTENSIONS_SUBTITLE */

#include <QCollator>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#define MATCHES_KEY "Redesign/SubtitleMatches"
#define CHECKED_ROLE ( Qt::UserRole + 1 )

namespace {

QString currentMediaPath( intf_thread_t *p_intf )
{
    input_item_t *p_item = THEMIM->currentInputItem();
    char *psz_uri = p_item ? input_item_GetURI( p_item ) : NULL;
    if( !psz_uri )
        return QString();
    const QUrl url( qfu( psz_uri ) );
    free( psz_uri );
    return url.isLocalFile() ? QDir::cleanPath( url.toLocalFile() ) : QString();
}

QStringList subtitleFiles( const QString &folder )
{
    QStringList files = QDir( folder ).entryList(
        QString( EXTENSIONS_SUBTITLE ).split( ';', QString::SkipEmptyParts ), QDir::Files | QDir::Readable );
    QCollator collator;
    collator.setNumericMode( true );
    collator.setCaseSensitivity( Qt::CaseInsensitive );
    std::sort( files.begin(), files.end(),
               [&collator]( const QString &a, const QString &b ) { return collator.compare( a, b ) < 0; } );
    return files;
}

/* "S01E02", "1x02", "E02", "Ep 2" or the last small number: season and episode */
QString episodeKey( const QString &fileName )
{
    const QString name = QFileInfo( fileName ).completeBaseName();
    static const QRegularExpression patterns[] = {
        QRegularExpression( "[Ss](\\d{1,2})[ ._-]*[Ee](\\d{1,3})" ),
        QRegularExpression( "(\\d{1,2})[xX](\\d{1,3})" ),
    };
    for( const QRegularExpression &re : patterns )
    {
        const QRegularExpressionMatch m = re.match( name );
        if( m.hasMatch() )
            return QString( "%1:%2" ).arg( m.captured( 1 ).toInt() ).arg( m.captured( 2 ).toInt() );
    }
    const QRegularExpressionMatch ep = QRegularExpression( "(?:[Ee][Pp]?|[Ee]pisode)[ ._-]*(\\d{1,3})" ).match( name );
    if( ep.hasMatch() )
        return QString( ":%1" ).arg( ep.captured( 1 ).toInt() );
    /* last number of 1 to 3 digits that is not a resolution (1080p) */
    QRegularExpressionMatchIterator it =
        QRegularExpression( "(?<![\\d])(\\d{1,3})(?![\\dpP])" ).globalMatch( name );
    QString last;
    while( it.hasNext() )
        last = it.next().captured( 1 );
    return last.isEmpty() ? QString() : QString( ":%1" ).arg( last.toInt() );
}

/* The subtitle of a media: same episode, or a name starting like the media */
QString matchSubtitle( const QString &media, const QStringList &subs )
{
    const QString base = QFileInfo( media ).completeBaseName();
    foreach( const QString &sub, subs )
        if( QFileInfo( sub ).completeBaseName().startsWith( base, Qt::CaseInsensitive ) )
            return sub;
    const QString key = episodeKey( media );
    if( key.isEmpty() )
        return QString();
    const bool b_seasonless = key.startsWith( ':' );
    foreach( const QString &sub, subs )
    {
        const QString subKey = episodeKey( sub );
        if( subKey == key || ( b_seasonless && subKey.endsWith( key ) )
                          || ( subKey.startsWith( ':' ) && key.endsWith( subKey ) ) )
            return sub;
    }
    return QString();
}

bool addSubtitle( input_thread_t *p_input, const QString &path, bool b_select )
{
    return p_input && input_AddSlave( p_input, SLAVE_TYPE_SPU,
                                      qtu( QUrl::fromLocalFile( path ).toString() ),
                                      b_select, true, false ) == VLC_SUCCESS;
}

/* "📄 name ............ ✓" */
class SubtitleFileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint( QPainter *painter, const QStyleOptionViewItem &option,
                const QModelIndex &index ) const Q_DECL_OVERRIDE
    {
        static const QIcon fileIcon( ":/redesign/sub-file.svg" );
        static const QIcon checkIcon( ":/redesign/check.svg" );
        painter->save();
        painter->setRenderHint( QPainter::Antialiasing );
        const QRect r = option.rect.adjusted( 0, 2, 0, -2 );
        const bool b_checked = index.data( CHECKED_ROLE ).toBool();
        if( b_checked || ( option.state & QStyle::State_MouseOver ) )
        {
            painter->setPen( Qt::NoPen );
            painter->setBrush( b_checked ? Redesign::BgB2 : Redesign::DividerDim );
            painter->drawRoundedRect( r, 4, 4 );
        }
        fileIcon.paint( painter, QRect( r.left() + 8, r.center().y() - 11, 24, 24 ) );
        if( b_checked )
            checkIcon.paint( painter, QRect( r.right() - 32, r.center().y() - 11, 24, 24 ) );
        QFont font = option.font;
        font.setPixelSize( 14 );
        painter->setFont( font );
        painter->setPen( Redesign::FgA1 );
        painter->drawText( r.adjusted( 44, 0, -44, 0 ), Qt::AlignVCenter | Qt::AlignLeft,
                           QFontMetrics( font ).elidedText( index.data().toString(), Qt::ElideMiddle, r.width() - 88 ) );
        painter->restore();
    }

    QSize sizeHint( const QStyleOptionViewItem &option, const QModelIndex & ) const Q_DECL_OVERRIDE
    {
        return QSize( option.rect.width(), 38 );
    }
};

} // namespace

AddSubtitlesDialog::AddSubtitlesDialog( intf_thread_t *_p_intf, QWidget *parent )
                  : QDialog( parent ), p_intf( _p_intf )
{
    setWindowTitle( qtr( "Add Subtitles" ) );
    setWindowFlags( windowFlags() & ~Qt::WindowContextHelpButtonHint );
    setObjectName( "addSubtitles" );
    resize( 1000, 760 );

    mediaPath = currentMediaPath( p_intf );
    subFolder = mediaPath.isEmpty() ? QDir::homePath() : QFileInfo( mediaPath ).absolutePath();

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    /* [To This File v] [From Computer v] [📁 Videos / Shows / The Wand / S01] */
    QWidget *header = new QWidget;
    header->setObjectName( "subsHeader" );
    QHBoxLayout *headerLayout = new QHBoxLayout( header );
    headerLayout->setContentsMargins( 10, 10, 10, 10 );
    headerLayout->setSpacing( 10 );
    targetCombo = new QComboBox;
    targetCombo->addItem( qtr( "To This File" ) );
    targetCombo->addItem( qtr( "To This Folder" ) );
    targetCombo->addItem( qtr( "To This Playlist" ) );
    targetCombo->setFixedWidth( 200 );
    headerLayout->addWidget( targetCombo );
    sourceCombo = new QComboBox;
    sourceCombo->addItem( qtr( "From Computer" ) );
    sourceCombo->addItem( qtr( "From OpenSubtitles" ) );
    /* searching OpenSubtitles needs its online API and an account */
    if( QStandardItemModel *sourceModel = qobject_cast<QStandardItemModel *>( sourceCombo->model() ) )
    {
        sourceModel->item( 1 )->setEnabled( false );
        sourceModel->item( 1 )->setToolTip( qtr( "Not available yet" ) );
    }
    sourceCombo->setFixedWidth( 200 );
    headerLayout->addWidget( sourceCombo );
    folderButton = new QToolButton;
    folderButton->setObjectName( "subsFolder" );
    folderButton->setIcon( QIcon( ":/redesign/folder.svg" ) );
    folderButton->setIconSize( QSize( 24, 24 ) );
    folderButton->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    folderButton->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    folderButton->setToolTip( qtr( "Folder of the subtitles" ) );
    connect( folderButton, &QToolButton::clicked, this, &AddSubtitlesDialog::chooseFolder );
    headerLayout->addWidget( folderButton, 1 );
    layout->addWidget( header );

    pages = new QStackedWidget;

    /* This file: subtitle files to pick */
    fileList = new QListWidget;
    fileList->setObjectName( "subsList" );
    fileList->setItemDelegate( new SubtitleFileDelegate( fileList ) );
    fileList->setFrameShape( QFrame::NoFrame );
    fileList->setSelectionMode( QAbstractItemView::NoSelection );
    fileList->viewport()->setAttribute( Qt::WA_Hover );
    connect( fileList, &QListWidget::itemClicked, this, []( QListWidgetItem *item ) {
        item->setData( CHECKED_ROLE, !item->data( CHECKED_ROLE ).toBool() );
    } );
    pages->addWidget( fileList );

    /* This folder / the queue: one row per media */
    QScrollArea *scroll = new QScrollArea;
    scroll->setObjectName( "subsScroll" );
    scroll->setFrameShape( QFrame::NoFrame );
    scroll->setWidgetResizable( true );
    QWidget *matchPage = new QWidget;
    matchPage->setObjectName( "subsMatchPage" );
    matchLayout = new QVBoxLayout( matchPage );
    matchLayout->setContentsMargins( 20, 16, 20, 16 );
    matchLayout->setSpacing( 4 );
    scroll->setWidget( matchPage );
    pages->addWidget( scroll );

    QWidget *body = new QWidget;
    QVBoxLayout *bodyLayout = new QVBoxLayout( body );
    bodyLayout->setContentsMargins( 20, 16, 20, 0 );
    bodyLayout->addWidget( pages );
    layout->addWidget( body, 1 );

    /* [Cancel]                                [Save] */
    QHBoxLayout *footer = new QHBoxLayout;
    footer->setContentsMargins( 20, 16, 20, 16 );
    QToolButton *cancel = new QToolButton;
    cancel->setObjectName( "subsCancel" );
    cancel->setText( qtr( "Cancel" ) );
    connect( cancel, &QToolButton::clicked, this, &QDialog::reject );
    QToolButton *saveButton = new QToolButton;
    saveButton->setObjectName( "subsSave" );
    saveButton->setText( qtr( "Save" ) );
    connect( saveButton, &QToolButton::clicked, this, &AddSubtitlesDialog::save );
    footer->addWidget( cancel );
    footer->addStretch( 1 );
    footer->addWidget( saveButton );
    layout->addLayout( footer );

    connect( targetCombo, static_cast<void (QComboBox::*)(int)>( &QComboBox::currentIndexChanged ),
             this, &AddSubtitlesDialog::rebuild );
    updateFolderButton();
    rebuild();
}

void AddSubtitlesDialog::open( intf_thread_t *p_intf, QWidget *parent )
{
    AddSubtitlesDialog dialog( p_intf, parent );
    Redesign::darkTitleBar( &dialog );
    dialog.exec();
}

void AddSubtitlesDialog::updateFolderButton()
{
    /* "Videos / Shows / The Wand / S01": the last levels of the folder */
    const QStringList parts = QDir::toNativeSeparators( subFolder ).split( QDir::separator(), QString::SkipEmptyParts );
    folderButton->setText( parts.mid( qMax( 0, parts.count() - 4 ) ).join( " / " ) );
}

void AddSubtitlesDialog::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory( this, qtr( "Folder of the subtitles" ), subFolder );
    if( folder.isEmpty() )
        return;
    subFolder = QDir::cleanPath( folder );
    updateFolderButton();
    rebuild();
}

QStringList AddSubtitlesDialog::targetMedia() const
{
    QStringList media;
    if( targetCombo->currentIndex() == TARGET_FOLDER )
    {
        if( mediaPath.isEmpty() )
            return media;
        const QString folder = QFileInfo( mediaPath ).absolutePath();
        foreach( const QString &file, MainInputManager::folderMedia( folder ) )
            if( !subtitleFiles( folder ).contains( file ) )
                media << QDir( folder ).filePath( file );
    }
    else if( targetCombo->currentIndex() == TARGET_QUEUE )
    {
        PLModel *model = PLModel::getPLModel( p_intf );
        const QModelIndex root = model->rootIndex();
        for( int i = 0; i < model->rowCount( root ); i++ )
        {
            const QUrl url( model->getURI( model->index( i, 0, root ) ) );
            if( url.isLocalFile() )
                media << QDir::cleanPath( url.toLocalFile() );
        }
    }
    return media;
}

void AddSubtitlesDialog::rebuild()
{
    const QStringList subs = subtitleFiles( subFolder );

    if( targetCombo->currentIndex() == TARGET_FILE )
    {
        fileList->clear();
        const QString preselected = mediaPath.isEmpty() ? QString() : matchSubtitle( mediaPath, subs );
        foreach( const QString &sub, subs )
        {
            QListWidgetItem *item = new QListWidgetItem( sub, fileList );
            item->setData( CHECKED_ROLE, sub == preselected );
        }
        pages->setCurrentIndex( 0 );
        return;
    }

    /* one "media ---- subtitle" row per media */
    QLayoutItem *child;
    while( ( child = matchLayout->takeAt( 0 ) ) )
    {
        delete child->widget();
        delete child;
    }
    matches.clear();

    const QVariantMap saved = getSettings()->value( MATCHES_KEY ).toMap();
    foreach( const QString &media, targetMedia() )
    {
        QWidget *row = new QWidget;
        QHBoxLayout *rowLayout = new QHBoxLayout( row );
        rowLayout->setContentsMargins( 0, 4, 0, 4 );
        rowLayout->setSpacing( 12 );
        QLabel *videoIcon = new QLabel;
        videoIcon->setPixmap( QIcon( ":/redesign/video-file.svg" ).pixmap( 24, 24 ) );
        rowLayout->addWidget( videoIcon );
        QLabel *name = new QLabel( QFileInfo( media ).fileName() );
        name->setObjectName( "subsMedia" );
        name->setMinimumWidth( 120 );
        rowLayout->addWidget( name );
        QFrame *dash = new QFrame;
        dash->setObjectName( "subsDash" );
        dash->setFixedHeight( 1 );
        dash->setMinimumWidth( 30 );
        rowLayout->addWidget( dash, 1 );
        QLabel *subIcon = new QLabel;
        subIcon->setPixmap( QIcon( ":/redesign/sub-file.svg" ).pixmap( 24, 24 ) );
        rowLayout->addWidget( subIcon );
        QComboBox *choice = new QComboBox;
        choice->setObjectName( "subsChoice" );
        choice->setFixedWidth( 320 );
        choice->addItem( qtr( "No subtitle" ), QString() );
        foreach( const QString &sub, subs )
            choice->addItem( sub, QDir( subFolder ).filePath( sub ) );

        const QString known = saved.value( QDir::toNativeSeparators( media ) ).toString();
        const QString match = matchSubtitle( media, subs );
        int index = known.isEmpty() ? -1 : choice->findData( QDir::cleanPath( known ) );
        if( index < 0 && !match.isEmpty() )
            index = choice->findText( match );
        choice->setCurrentIndex( qMax( 0, index ) );
        rowLayout->addWidget( choice );

        matchLayout->addWidget( row );
        matches << qMakePair( media, choice );
    }
    if( matches.isEmpty() )
    {
        QLabel *empty = new QLabel( targetCombo->currentIndex() == TARGET_QUEUE
                                    ? qtr( "The queue has no local media." )
                                    : qtr( "Open a media of the folder first." ) );
        empty->setObjectName( "subsMedia" );
        matchLayout->addWidget( empty );
    }
    matchLayout->addStretch( 1 );
    pages->setCurrentIndex( 1 );
}

void AddSubtitlesDialog::save()
{
    input_thread_t *p_input = THEMIM->getInput();

    if( targetCombo->currentIndex() == TARGET_FILE )
    {
        bool b_first = true;
        for( int i = 0; i < fileList->count(); i++ )
        {
            QListWidgetItem *item = fileList->item( i );
            if( !item->data( CHECKED_ROLE ).toBool() )
                continue;
            const QString path = QDir( subFolder ).filePath( item->text() );
            addSubtitle( p_input, path, b_first );
            if( b_first && !mediaPath.isEmpty() )
            {
                /* remembered for the next time this file is opened */
                QVariantMap saved = getSettings()->value( MATCHES_KEY ).toMap();
                saved.insert( QDir::toNativeSeparators( mediaPath ), QDir::toNativeSeparators( path ) );
                getSettings()->setValue( MATCHES_KEY, saved );
            }
            b_first = false;
        }
        accept();
        return;
    }

    QVariantMap saved = getSettings()->value( MATCHES_KEY ).toMap();
    for( int i = 0; i < matches.count(); i++ )
    {
        const QString media = QDir::toNativeSeparators( matches.at( i ).first );
        const QString sub = matches.at( i ).second->currentData().toString();
        if( sub.isEmpty() )
            saved.remove( media );
        else
        {
            saved.insert( media, QDir::toNativeSeparators( sub ) );
            if( QDir::cleanPath( matches.at( i ).first ) == mediaPath )
                addSubtitle( p_input, sub, true );
        }
    }
    getSettings()->setValue( MATCHES_KEY, saved );
    accept();
}

void AddSubtitlesDialog::applySavedSubtitles( intf_thread_t *p_intf )
{
    const QString media = currentMediaPath( p_intf );
    if( media.isEmpty() )
        return;
    const QString sub = getSettings()->value( MATCHES_KEY ).toMap()
                            .value( QDir::toNativeSeparators( media ) ).toString();
    if( sub.isEmpty() || !QFileInfo( sub ).exists() )
        return;
    /* VLC already loads the subtitles named like the media, next to it */
    const QFileInfo subInfo( sub ), mediaInfo( media );
    if( subInfo.absolutePath() == mediaInfo.absolutePath() &&
        subInfo.completeBaseName().startsWith( mediaInfo.completeBaseName(), Qt::CaseInsensitive ) )
        return;
    addSubtitle( THEMIM->getInput(), QDir::fromNativeSeparators( sub ), true );
}
