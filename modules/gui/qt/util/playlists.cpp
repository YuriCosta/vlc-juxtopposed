/*****************************************************************************
 * playlists.cpp : the user's saved playlists (XSPF files)
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

#include "util/playlists.hpp"

#include <vlc_configuration.h> /* config_GetUserDir */

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

namespace {

bool write( const QString &path, const Playlists::Info &info )
{
    QSaveFile file( path );
    if( !file.open( QIODevice::WriteOnly ) )
        return false;
    QXmlStreamWriter xml( &file );
    xml.setAutoFormatting( true );
    xml.writeStartDocument();
    xml.writeStartElement( "playlist" );
    xml.writeDefaultNamespace( "http://xspf.org/ns/0/" );
    xml.writeAttribute( "version", "1" );
    xml.writeTextElement( "title", info.title );
    if( !info.image.isEmpty() )
        xml.writeTextElement( "image", QUrl::fromLocalFile( info.image ).toString() );
    xml.writeStartElement( "trackList" );
    foreach( const QString &url, info.tracks )
    {
        xml.writeStartElement( "track" );
        xml.writeTextElement( "location", url );
        xml.writeEndElement();
    }
    xml.writeEndElement(); /* trackList */
    xml.writeEndElement(); /* playlist */
    xml.writeEndDocument();
    return file.commit();
}

} // namespace

QString Playlists::folder( intf_thread_t * )
{
    char *psz_dir = config_GetUserDir( VLC_DATA_DIR );
    const QString dir = QDir( qfu( psz_dir ) ).filePath( "playlists" );
    free( psz_dir );
    QDir().mkpath( dir );
    return dir;
}

QStringList Playlists::list( intf_thread_t *p_intf )
{
    const QDir dir( folder( p_intf ) );
    QStringList files = dir.entryList( QStringList() << "*.xspf", QDir::Files );
    QCollator collator;
    collator.setNumericMode( true );
    collator.setCaseSensitivity( Qt::CaseInsensitive );
    std::sort( files.begin(), files.end(),
               [&collator]( const QString &a, const QString &b ) { return collator.compare( a, b ) < 0; } );
    QStringList paths;
    foreach( const QString &file, files )
        paths << dir.filePath( file );
    return paths;
}

Playlists::Info Playlists::read( const QString &path )
{
    Info info;
    QFile file( path );
    if( file.open( QIODevice::ReadOnly ) )
    {
        QXmlStreamReader xml( &file );
        bool b_inTrack = false;
        while( !xml.atEnd() )
        {
            xml.readNext();
            if( xml.isStartElement() )
            {
                const QStringRef name = xml.name();
                if( name == "track" )
                    b_inTrack = true;
                else if( name == "location" && b_inTrack )
                    info.tracks << xml.readElementText();
                else if( name == "title" && !b_inTrack && info.title.isEmpty() )
                    info.title = xml.readElementText();
                else if( name == "image" && !b_inTrack )
                    info.image = QUrl( xml.readElementText() ).toLocalFile();
            }
            else if( xml.isEndElement() && xml.name() == "track" )
                b_inTrack = false;
        }
    }
    if( info.title.isEmpty() )
        info.title = QFileInfo( path ).completeBaseName();
    return info;
}

QString Playlists::create( intf_thread_t *p_intf, const QString &title, const QString &image,
                           const QStringList &trackUrls )
{
    /* a file name from the title, unique in the folder */
    QString base = title.trimmed();
    base.replace( QRegularExpression( "[\\\\/:*?\"<>|]" ), "_" );
    if( base.isEmpty() )
        base = "Playlist";
    const QDir dir( folder( p_intf ) );
    QString path = dir.filePath( base + ".xspf" );
    for( int i = 2; QFileInfo::exists( path ); i++ )
        path = dir.filePath( QString( "%1 (%2).xspf" ).arg( base ).arg( i ) );

    Info info;
    info.title = title.trimmed();
    info.image = image;
    info.tracks = trackUrls;
    return write( path, info ) ? path : QString();
}

bool Playlists::append( const QString &path, const QStringList &trackUrls )
{
    Info info = read( path );
    info.tracks << trackUrls;
    return write( path, info );
}

bool Playlists::removeTrack( const QString &path, int index )
{
    Info info = read( path );
    if( index < 0 || index >= info.tracks.count() )
        return false;
    info.tracks.removeAt( index );
    return write( path, info );
}

bool Playlists::remove( const QString &path )
{
    return QFile::remove( path );
}
