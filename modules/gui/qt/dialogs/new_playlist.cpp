/*****************************************************************************
 * new_playlist.cpp : "New Playlist" dialog of the redesign
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

#include "dialogs/new_playlist.hpp"
#include "util/playlists.hpp"
#include "util/redesign.hpp"

#include <vlc_interface.h> /* EXTENSIONS_MEDIA */

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QLabel *heading( const QString &title, const QString &text, QVBoxLayout *layout )
{
    QLabel *label = new QLabel( title );
    label->setObjectName( "plHeading" );
    layout->addWidget( label );
    QLabel *desc = new QLabel( text );
    desc->setObjectName( "plDesc" );
    desc->setWordWrap( true );
    layout->addWidget( desc );
    return label;
}

QString displayName( const QString &url )
{
    const QUrl u( url );
    return u.isLocalFile() ? QFileInfo( u.toLocalFile() ).fileName() : url;
}

} // namespace

QString NewPlaylistDialog::create( intf_thread_t *p_intf, QWidget *parent, const QStringList &trackUrls )
{
    NewPlaylistDialog dialog( p_intf, parent );
    dialog.addTracks( trackUrls );
    if( dialog.exec() != QDialog::Accepted )
        return QString();
    const QString path = Playlists::create( p_intf, dialog.nameEdit->text(), dialog.imagePath, dialog.tracks );
    if( path.isEmpty() )
        QMessageBox::warning( parent, qtr( "New Playlist" ), qtr( "The playlist could not be saved." ) );
    return path;
}

NewPlaylistDialog::NewPlaylistDialog( intf_thread_t *_p_intf, QWidget *parent )
                 : QDialog( parent ), p_intf( _p_intf )
{
    setWindowTitle( qtr( "New Playlist" ) );
    setWindowFlags( windowFlags() & ~Qt::WindowContextHelpButtonHint );
    setObjectName( "newPlaylist" );
    setAcceptDrops( true );
    resize( 740, 560 );
    Redesign::darkTitleBar( this );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 20, 22, 20, 20 );
    layout->setSpacing( 20 );

    /* Playlist details                                   [ + ]
     * [e.g., Favorite Videos                         ]          */
    QHBoxLayout *detailsLayout = new QHBoxLayout;
    detailsLayout->setSpacing( 20 );
    QVBoxLayout *detailsText = new QVBoxLayout;
    detailsText->setSpacing( 5 );
    heading( qtr( "Playlist Details" ),
             qtr( "The playlist will be saved and displayed with this title and thumbnail." ), detailsText );
    detailsText->addSpacing( 15 );
    nameEdit = new QLineEdit;
    nameEdit->setObjectName( "plName" );
    nameEdit->setPlaceholderText( qtr( "e.g., Favorite Videos" ) );
    connect( nameEdit, &QLineEdit::textChanged, this, &NewPlaylistDialog::updateState );
    detailsText->addWidget( nameEdit );
    detailsLayout->addLayout( detailsText, 1 );

    imageButton = new QToolButton;
    imageButton->setObjectName( "plImage" );
    imageButton->setFixedSize( 112, 112 );
    imageButton->setIcon( QIcon( ":/redesign/lib-add-small.svg" ) );
    imageButton->setIconSize( QSize( 24, 24 ) );
    imageButton->setToolTip( qtr( "Choose a thumbnail" ) );
    imageButton->setCursor( Qt::PointingHandCursor );
    connect( imageButton, &QToolButton::clicked, this, &NewPlaylistDialog::chooseImage );
    detailsLayout->addWidget( imageButton, 0, Qt::AlignBottom );
    layout->addLayout( detailsLayout );

    QFrame *divider = new QFrame;
    divider->setObjectName( "plDivider" );
    divider->setFixedHeight( 1 );
    layout->addWidget( divider );

    /* Select files: drop them in the box or click it */
    QVBoxLayout *filesText = new QVBoxLayout;
    filesText->setSpacing( 5 );
    heading( qtr( "Select Files" ), qtr( "These files will be added to the playlist." ), filesText );
    layout->addLayout( filesText );

    dropArea = new QFrame;
    dropArea->setObjectName( "plDrop" );
    dropArea->setCursor( Qt::PointingHandCursor );
    dropArea->setMinimumHeight( 186 );
    dropArea->installEventFilter( this );
    QVBoxLayout *dropLayout = new QVBoxLayout( dropArea );
    dropLayout->setContentsMargins( 12, 12, 12, 12 );
    dropLayout->setSpacing( 12 );
    fileList = new QListWidget;
    fileList->setObjectName( "plFiles" );
    fileList->setFrameShape( QFrame::NoFrame );
    fileList->setSelectionMode( QAbstractItemView::NoSelection );
    fileList->setFocusPolicy( Qt::NoFocus );
    fileList->hide();
    dropLayout->addWidget( fileList, 1 );
    dropLabel = new QLabel( qtr( "Drag files into the box or click here to select files." ) );
    dropLabel->setObjectName( "plDropText" );
    dropLabel->setAlignment( Qt::AlignCenter );
    dropLayout->addStretch( 1 );
    dropLayout->addWidget( dropLabel );
    QToolButton *addButton = new QToolButton;
    addButton->setObjectName( "plAdd" );
    addButton->setIcon( QIcon( ":/redesign/lib-add-small.svg" ) );
    addButton->setIconSize( QSize( 24, 24 ) );
    addButton->setToolTip( qtr( "Select files" ) );
    connect( addButton, &QToolButton::clicked, this, &NewPlaylistDialog::chooseFiles );
    dropLayout->addWidget( addButton, 0, Qt::AlignHCenter );
    dropLayout->addStretch( 1 );
    layout->addWidget( dropArea, 1 );

    /* [Cancel]                                [Save] */
    QHBoxLayout *footer = new QHBoxLayout;
    QToolButton *cancel = new QToolButton;
    cancel->setObjectName( "plCancel" );
    cancel->setText( qtr( "Cancel" ) );
    connect( cancel, &QToolButton::clicked, this, &QDialog::reject );
    saveButton = new QToolButton;
    saveButton->setObjectName( "plSave" );
    saveButton->setText( qtr( "Save" ) );
    connect( saveButton, &QToolButton::clicked, this, &QDialog::accept );
    footer->addWidget( cancel );
    footer->addStretch( 1 );
    footer->addWidget( saveButton );
    layout->addLayout( footer );

    nameEdit->setFocus();
    updateState();
}

void NewPlaylistDialog::updateState()
{
    /* Figma: Save stays dimmed until the playlist has a title */
    saveButton->setEnabled( !nameEdit->text().trimmed().isEmpty() );
    fileList->setVisible( !tracks.isEmpty() );
    dropLabel->setVisible( tracks.isEmpty() );
}

void NewPlaylistDialog::chooseImage()
{
    const QString file = QFileDialog::getOpenFileName( this, qtr( "Choose a thumbnail" ),
            imagePath.isEmpty() ? QString() : QFileInfo( imagePath ).absolutePath(),
            qtr( "Images" ) + " (*.png *.jpg *.jpeg *.bmp *.gif *.webp)" );
    if( file.isEmpty() )
        return;
    const QPixmap image( file );
    if( image.isNull() )
        return;
    imagePath = file;
    imageButton->setIcon( QIcon( image.scaled( QSize( 108, 108 ), Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation ).copy( 0, 0, 108, 108 ) ) );
    imageButton->setIconSize( QSize( 108, 108 ) );
}

void NewPlaylistDialog::chooseFiles()
{
    const QString filter = qtr( "Media Files" ) + " (" + QString( EXTENSIONS_MEDIA ).replace( ';', ' ' ) + ");;"
                         + qtr( "All Files" ) + " (*)";
    const QList<QUrl> urls = QFileDialog::getOpenFileUrls( this, qtr( "Select Files" ), QUrl(), filter );
    QStringList list;
    foreach( const QUrl &url, urls )
        list << url.toString( QUrl::FullyEncoded );
    addTracks( list );
}

void NewPlaylistDialog::addTracks( const QStringList &urls )
{
    foreach( const QString &url, urls )
    {
        if( tracks.contains( url ) )
            continue;
        tracks << url;
        QListWidgetItem *item = new QListWidgetItem( displayName( url ), fileList );
        item->setToolTip( QUrl( url ).isLocalFile() ? QDir::toNativeSeparators( QUrl( url ).toLocalFile() ) : url );
    }
    updateState();
}

void NewPlaylistDialog::dragEnterEvent( QDragEnterEvent *event )
{
    if( event->mimeData()->hasUrls() )
        event->acceptProposedAction();
}

void NewPlaylistDialog::dropEvent( QDropEvent *event )
{
    QStringList list;
    foreach( const QUrl &url, event->mimeData()->urls() )
        list << url.toString( QUrl::FullyEncoded );
    addTracks( list );
    event->acceptProposedAction();
}

/* A click anywhere in the box selects files */
bool NewPlaylistDialog::eventFilter( QObject *obj, QEvent *event )
{
    if( obj == dropArea && event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>( event )->button() == Qt::LeftButton )
    {
        chooseFiles();
        return true;
    }
    return QDialog::eventFilter( obj, event );
}
