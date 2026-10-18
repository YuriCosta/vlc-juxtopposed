/*****************************************************************************
 * seek_preview.cpp : frames of the media for the seek bar tooltip
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

#include "util/seek_preview.hpp"

#include <vlc_input.h>
#include <vlc_input_item.h>

#include <QTimer>

#include <atomic>

#define CACHE_SLICE   ( 2 * CLOCK_FREQ ) /* one frame per 2 s */
#define CACHE_SIZE    300
#define DEBOUNCE_MS   150
#define WATCHDOG_MS   4000

/* Shared with the vout thread of the preview input */
struct SeekPreviewJob
{
    SeekPreviewer    *owner;
    QString           key;
    QImage            image;
    std::atomic<bool> done;
    input_thread_t   *input;
};

/* vmem callbacks (vout thread) */
static void *previewLock( void *opaque, void **planes )
{
    SeekPreviewJob *job = static_cast<SeekPreviewJob *>( opaque );
    planes[0] = job->image.bits();
    return NULL;
}

static void previewUnlock( void *, void *, void *const * )
{
}

static void previewDisplay( void *opaque, void * )
{
    SeekPreviewJob *job = static_cast<SeekPreviewJob *>( opaque );
    if( job->done.exchange( true ) )
        return;
    QMetaObject::invokeMethod( job->owner, "deliver", Qt::QueuedConnection,
                               Q_ARG( QString, job->key ), Q_ARG( QImage, job->image.copy() ) );
}

static SeekPreviewer *previewerInstance = NULL;

SeekPreviewer *SeekPreviewer::instance( intf_thread_t *p_intf )
{
    if( !previewerInstance )
        previewerInstance = new SeekPreviewer( p_intf );
    return previewerInstance;
}

void SeekPreviewer::killInstance()
{
    delete previewerInstance;
    previewerInstance = NULL;
}

SeekPreviewer::SeekPreviewer( intf_thread_t *_p_intf )
             : p_intf( _p_intf ), pendingTime( 0 ), job( NULL )
{
    debounce = new QTimer( this );
    debounce->setSingleShot( true );
    debounce->setInterval( DEBOUNCE_MS );
    connect( debounce, &QTimer::timeout, this, &SeekPreviewer::startJob );

    watchdog = new QTimer( this );
    watchdog->setSingleShot( true );
    watchdog->setInterval( WATCHDOG_MS );
    connect( watchdog, &QTimer::timeout, this, &SeekPreviewer::stopJob );
}

SeekPreviewer::~SeekPreviewer()
{
    stopJob();
}

QString SeekPreviewer::cacheKey( const QString &mrl, int64_t time )
{
    return mrl + QLatin1Char( '#' ) + QString::number( time / CACHE_SLICE );
}

QImage SeekPreviewer::preview( const QString &mrl, int64_t time )
{
    const QString key = cacheKey( mrl, time );
    QHash<QString, QImage>::const_iterator it = cache.constFind( key );
    if( it != cache.constEnd() )
        return it.value();

    /* the slice is requested once the mouse rests a little */
    pendingMrl = mrl;
    pendingTime = ( time / CACHE_SLICE ) * CACHE_SLICE + CACHE_SLICE / 2;
    debounce->start();
    return QImage();
}

void SeekPreviewer::cancel()
{
    debounce->stop();
    pendingMrl.clear();
}

void SeekPreviewer::startJob()
{
    if( pendingMrl.isEmpty() )
        return;
    const QString key = cacheKey( pendingMrl, pendingTime );
    if( cache.contains( key ) )
        return;
    stopJob();

    input_item_t *item = input_item_New( qtu( pendingMrl ), NULL );
    if( !item )
        return;
    const QByteArray start = QString( ":start-time=%1" ).arg( pendingTime / (double)CLOCK_FREQ, 0, 'f', 3 ).toUtf8();
    const char *options[] = {
        ":vout=vmem", ":vmem-chroma=RV32", ":no-audio", ":no-spu",
        ":no-sub-autodetect-file", ":avcodec-hw=none", ":input-fast-seek",
        ":no-video-title-show", ":no-osd", start.constData(),
    };
    for( const char *option : options )
        input_item_AddOption( item, option, VLC_INPUT_OPTION_TRUSTED );
    input_item_AddOption( item, qtu( QString( ":vmem-width=%1" ).arg( WIDTH ) ), VLC_INPUT_OPTION_TRUSTED );
    input_item_AddOption( item, qtu( QString( ":vmem-height=%1" ).arg( HEIGHT ) ), VLC_INPUT_OPTION_TRUSTED );
    input_item_AddOption( item, qtu( QString( ":vmem-pitch=%1" ).arg( WIDTH * 4 ) ), VLC_INPUT_OPTION_TRUSTED );

    job = new SeekPreviewJob;
    job->owner = this;
    job->key = key;
    job->image = QImage( WIDTH, HEIGHT, QImage::Format_RGB32 );
    job->image.fill( Qt::black );
    job->done = false;
    job->input = input_Create( p_intf, item, "seek preview", NULL, NULL );
    input_item_Release( item );
    if( !job->input )
    {
        delete job;
        job = NULL;
        return;
    }

    /* inherited by the vout display of this input */
    vlc_object_t *obj = VLC_OBJECT( job->input );
    var_Create( obj, "vmem-lock", VLC_VAR_ADDRESS );
    var_SetAddress( obj, "vmem-lock", (void *)previewLock );
    var_Create( obj, "vmem-unlock", VLC_VAR_ADDRESS );
    var_SetAddress( obj, "vmem-unlock", (void *)previewUnlock );
    var_Create( obj, "vmem-display", VLC_VAR_ADDRESS );
    var_SetAddress( obj, "vmem-display", (void *)previewDisplay );
    var_Create( obj, "vmem-data", VLC_VAR_ADDRESS );
    var_SetAddress( obj, "vmem-data", job );

    if( input_Start( job->input ) != VLC_SUCCESS )
    {
        input_Close( job->input );
        delete job;
        job = NULL;
        return;
    }
    watchdog->start();
}

void SeekPreviewer::stopJob()
{
    watchdog->stop();
    if( !job )
        return;
    input_Stop( job->input );
    input_Close( job->input ); /* joins the decoder and vout threads */
    delete job;
    job = NULL;
}

void SeekPreviewer::deliver( const QString &key, const QImage &image )
{
    cache.insert( key, image );
    cacheOrder << key;
    while( cacheOrder.count() > CACHE_SIZE )
        cache.remove( cacheOrder.takeFirst() );
    if( job && job->key == key )
        QTimer::singleShot( 0, this, &SeekPreviewer::stopJob );
    emit previewReady();
}
