/*****************************************************************************
 * network_stream.cpp : "Open Network Stream" dialog of the redesign
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

#include "dialogs/network_stream.hpp"
#include "dialogs_provider.hpp"
#include "recents.hpp"              /* Open::openMRL */
#include "util/playlists.hpp"
#include "util/redesign.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

void NetworkStreamDialog::open( intf_thread_t *p_intf, QWidget *parent )
{
    NetworkStreamDialog *dialog = new NetworkStreamDialog( p_intf, parent );
    dialog->setAttribute( Qt::WA_DeleteOnClose );
    dialog->show();
}

NetworkStreamDialog::NetworkStreamDialog( intf_thread_t *_p_intf, QWidget *parent )
                   : QDialog( parent ), p_intf( _p_intf )
{
    setWindowTitle( qtr( "Open Network Stream" ) );
    setWindowFlags( windowFlags() & ~Qt::WindowContextHelpButtonHint );
    setObjectName( "newPlaylist" ); /* same look as the "New Playlist" dialog */
    resize( 1000, 600 );
    Redesign::darkTitleBar( this );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 20, 22, 20, 20 );
    layout->setSpacing( 20 );

    auto heading = [layout]( const QString &title, const QString &text ) {
        QVBoxLayout *box = new QVBoxLayout;
        box->setSpacing( 5 );
        QLabel *label = new QLabel( title );
        label->setObjectName( "plHeading" );
        box->addWidget( label );
        QLabel *desc = new QLabel( text );
        desc->setObjectName( "plDesc" );
        box->addWidget( desc );
        layout->addLayout( box );
    };

    heading( qtr( "Network URL" ), qtr( "You can enter multiple links to load, one per line." ) );
    urlEdit = new QPlainTextEdit;
    urlEdit->setObjectName( "plUrls" );
    urlEdit->setPlaceholderText( qtr( "e.g., https://www.example.com/stream.avi" ) );
    urlEdit->setMinimumHeight( 194 );
    connect( urlEdit, &QPlainTextEdit::textChanged, this, &NetworkStreamDialog::updateState );
    layout->addWidget( urlEdit, 1 );

    QFrame *divider = new QFrame;
    divider->setObjectName( "plDivider" );
    divider->setFixedHeight( 1 );
    layout->addWidget( divider );

    heading( qtr( "Playlist Name" ) + " <span style='color:#6B6B6B'>" + qtr( "(optional)" ) + "</span>",
             qtr( "The links will be saved as a playlist of the library with this title." ) );
    nameEdit = new QLineEdit;
    nameEdit->setObjectName( "plName" );
    nameEdit->setPlaceholderText( qtr( "e.g., Favorite TV Channels" ) );
    layout->addWidget( nameEdit );

    QHBoxLayout *footer = new QHBoxLayout;
    QToolButton *cancel = new QToolButton;
    cancel->setObjectName( "plCancel" );
    cancel->setText( qtr( "Cancel" ) );
    connect( cancel, &QToolButton::clicked, this, &QDialog::reject );
    openButton = new QToolButton;
    openButton->setObjectName( "plSave" );
    openButton->setText( qtr( "Play" ) );
    connect( openButton, &QToolButton::clicked, this, &NetworkStreamDialog::play );
    footer->addWidget( cancel );
    footer->addStretch( 1 );
    footer->addWidget( openButton );
    layout->addLayout( footer );

    /* like the classic dialog: a link in the clipboard is probably for us */
    const QString clip = QApplication::clipboard()->text().trimmed();
    const QUrl clipUrl( clip );
    if( !clip.contains( '\n' ) && clipUrl.isValid() && !clipUrl.scheme().isEmpty() &&
        clipUrl.scheme() != "file" && clipUrl.scheme().length() > 1 )
    {
        urlEdit->setPlainText( clip );
        urlEdit->selectAll();
    }
    urlEdit->setFocus();
    updateState();
}

QStringList NetworkStreamDialog::urls() const
{
    QStringList list;
    foreach( const QString &line, urlEdit->toPlainText().split( '\n' ) )
        if( !line.trimmed().isEmpty() )
            list << line.trimmed();
    return list;
}

void NetworkStreamDialog::updateState()
{
    openButton->setEnabled( !urls().isEmpty() );
}

void NetworkStreamDialog::play()
{
    const QStringList list = urls();
    if( list.isEmpty() )
        return;
    const QString name = nameEdit->text().trimmed();
    if( !name.isEmpty() && Playlists::create( p_intf, name, QString(), list ).isEmpty() )
        QMessageBox::warning( this, windowTitle(), qtr( "The playlist could not be saved." ) );
    for( int i = 0; i < list.count(); i++ )
        Open::openMRL( p_intf, list.at( i ), i == 0, true );
    accept();
}
