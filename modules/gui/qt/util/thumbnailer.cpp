/*****************************************************************************
 * thumbnailer.cpp : asynchronous media thumbnails for the redesigned library
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

#include "util/thumbnailer.hpp"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QThread>
#include <QWaitCondition>

#ifdef _WIN32
# include <windows.h>
# include <objbase.h>

/* IShellItemImageFactory and SHCreateItemFromParsingName are Vista+ and hidden
 * by the XP compatible headers VLC 3 is built with: declare and load them here */
namespace {
struct ShellItemImageFactory : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetImage( SIZE size, int flags, HBITMAP *phbm ) = 0;
};
const IID IID_ShellItemImageFactory =
    { 0xbcc18b79, 0xba16, 0x442f, { 0x80, 0xc4, 0x8a, 0x59, 0xc3, 0x0c, 0x46, 0x3b } };
enum { SIIGBF_BIGGERSIZEOK = 0x1, SIIGBF_THUMBNAILONLY = 0x8 };
typedef HRESULT (WINAPI *CreateItemFn)( PCWSTR, IBindCtx *, REFIID, void ** );

QImage shellThumbnail( CreateItemFn createItem, const QString &path )
{
    QImage image;
    if( !createItem )
        return image;

    const QString native = QDir::toNativeSeparators( path );
    ShellItemImageFactory *factory = NULL;
    if( FAILED( createItem( reinterpret_cast<PCWSTR>( native.utf16() ), NULL,
                            IID_ShellItemImageFactory, (void **)&factory ) ) || !factory )
        return image;

    HBITMAP hbm = NULL;
    const SIZE size = { 320, 180 };
    if( SUCCEEDED( factory->GetImage( size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hbm ) ) && hbm )
    {
        BITMAP bm;
        if( GetObject( hbm, sizeof( bm ), &bm ) && bm.bmWidth > 0 && bm.bmHeight > 0 )
        {
            BITMAPINFO bi;
            ZeroMemory( &bi, sizeof( bi ) );
            bi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
            bi.bmiHeader.biWidth = bm.bmWidth;
            bi.bmiHeader.biHeight = -bm.bmHeight; /* top-down */
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            QImage argb( bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied );
            HDC dc = GetDC( NULL );
            const int lines = GetDIBits( dc, hbm, 0, bm.bmHeight, argb.bits(), &bi, DIB_RGB_COLORS );
            ReleaseDC( NULL, dc );
            if( lines == bm.bmHeight )
            {
                /* Most video thumbnails are opaque bitmaps with a zero alpha */
                bool b_alpha = false;
                for( int y = 0; y < argb.height() && !b_alpha; y++ )
                {
                    const QRgb *line = reinterpret_cast<const QRgb *>( argb.constScanLine( y ) );
                    for( int x = 0; x < argb.width(); x++ )
                        if( qAlpha( line[x] ) ) { b_alpha = true; break; }
                }
                if( !b_alpha )
                    for( int y = 0; y < argb.height(); y++ )
                    {
                        QRgb *line = reinterpret_cast<QRgb *>( argb.scanLine( y ) );
                        for( int x = 0; x < argb.width(); x++ )
                            line[x] |= 0xFF000000;
                    }
                image = argb;
            }
        }
        DeleteObject( hbm );
    }
    factory->Release();
    return image;
}
} // namespace
#endif

/* Serves the requests in the background, most recent first */
class ThumbnailerThread : public QThread
{
public:
    explicit ThumbnailerThread( Thumbnailer *_owner ) : owner( _owner ), b_stop( false ) {}

    void request( const QString &path )
    {
        QMutexLocker locker( &lock );
        queue.append( path );
        wait.wakeOne();
    }

    void stop()
    {
        {
            QMutexLocker locker( &lock );
            b_stop = true;
            wait.wakeAll();
        }
        QThread::wait();
    }

protected:
    void run() Q_DECL_OVERRIDE
    {
#ifdef _WIN32
        CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
        CreateItemFn createItem = (CreateItemFn)
            GetProcAddress( GetModuleHandleW( L"shell32.dll" ), "SHCreateItemFromParsingName" );
#endif
        for( ;; )
        {
            QString path;
            {
                QMutexLocker locker( &lock );
                while( queue.isEmpty() && !b_stop )
                    wait.wait( &lock );
                if( b_stop )
                    break;
                /* the visible items are the last requested ones */
                path = queue.takeLast();
            }

            QImage image;
#ifdef _WIN32
            image = shellThumbnail( createItem, path );
#endif
            QMetaObject::invokeMethod( owner, "store", Qt::QueuedConnection,
                                       Q_ARG( QString, path ), Q_ARG( QImage, image ) );
        }
#ifdef _WIN32
        CoUninitialize();
#endif
    }

private:
    Thumbnailer   *owner;
    QMutex         lock;
    QWaitCondition wait;
    QStringList    queue;
    bool           b_stop;
};

static Thumbnailer *thumbnailerInstance = NULL;

Thumbnailer *Thumbnailer::instance()
{
    if( !thumbnailerInstance )
        thumbnailerInstance = new Thumbnailer;
    return thumbnailerInstance;
}

void Thumbnailer::killInstance()
{
    delete thumbnailerInstance;
    thumbnailerInstance = NULL;
}

Thumbnailer::Thumbnailer()
{
    worker = new ThumbnailerThread( this );
    worker->start( QThread::LowPriority );
}

Thumbnailer::~Thumbnailer()
{
    worker->stop();
    delete worker;
}

QPixmap Thumbnailer::thumbnail( const QString &path )
{
    QHash<QString, QPixmap>::const_iterator it = cache.constFind( path );
    if( it != cache.constEnd() )
        return it.value();
    if( path.isEmpty() || failed.contains( path ) || requested.contains( path ) )
        return QPixmap();

    requested.insert( path );
    worker->request( path );
    return QPixmap();
}

void Thumbnailer::store( const QString &path, const QImage &image )
{
    requested.remove( path );
    if( image.isNull() )
    {
        failed.insert( path );
        return;
    }
    cache.insert( path, QPixmap::fromImage( image ) );
    emit thumbnailReady( path );
}
