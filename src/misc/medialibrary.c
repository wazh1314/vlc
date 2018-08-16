/*****************************************************************************
 * medialib.cpp: medialibrary module
 *****************************************************************************
 * Copyright © 2015-2016 VLC authors, VideoLAN and VideoLabs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_media_library.h>
#include <vlc_modules.h>
#include <vlc_list.h>
#include <vlc_threads.h>
#include <libvlc.h>

#include <assert.h>

struct vlc_ml_event_callback_t
{
    vlc_ml_callback_t pf_cb;
    void* p_data;
    struct vlc_list node;
};

struct vlc_medialibrary_t
{
    vlc_medialibrary_module_t m;

    vlc_mutex_t lock;
    struct vlc_list cbs;
};

static vlc_medialibrary_t* ml_priv( vlc_medialibrary_module_t* p_ml )
{
    return container_of( p_ml, struct vlc_medialibrary_t, m );
}

void vlc_ml_entrypoints_release( vlc_ml_entrypoint_t* p_list, size_t i_nb_items )
{
    for ( size_t i = 0; i < i_nb_items; ++i )
    {
        free( p_list[i].psz_mrl );
    }
    free( p_list );
}

static void vlc_ml_event_send( vlc_medialibrary_module_t* p_ml, const vlc_ml_event_t* p_event )
{
    vlc_medialibrary_t* p_priv = ml_priv( p_ml );
    vlc_mutex_lock( &p_priv->lock );
    struct vlc_ml_event_callback_t* p_cb;
    vlc_list_foreach( p_cb, &p_priv->cbs, node )
    {
        p_cb->pf_cb( p_cb->p_data, p_event );
    }
    vlc_mutex_unlock( &p_priv->lock );
}

vlc_ml_event_callback_t*
vlc_ml_event_register_callback( vlc_medialibrary_t* p_ml, vlc_ml_callback_t cb,
                                void* p_data )
{
    struct vlc_ml_event_callback_t* p_cb = malloc( sizeof( *p_cb ) );
    if ( unlikely( p_cb == NULL ) )
        return NULL;
    p_cb->pf_cb = cb;
    p_cb->p_data = p_data;
    vlc_mutex_lock( &p_ml->lock );
    vlc_list_append( &p_cb->node, &p_ml->cbs );
    vlc_mutex_unlock( &p_ml->lock );
    return p_cb;
}

void vlc_ml_event_unregister_callback( vlc_medialibrary_t* p_ml,
                                       vlc_ml_event_callback_t* p_cb )
{
    vlc_mutex_lock( &p_ml->lock );
    vlc_list_remove( &p_cb->node );
    vlc_mutex_unlock( &p_ml->lock );
    free( p_cb );
}

static const vlc_medialibrary_callbacks_t callbacks = {
    .pf_send_event = &vlc_ml_event_send
};

vlc_medialibrary_t* libvlc_MlCreate( libvlc_int_t* p_libvlc  )
{
    vlc_medialibrary_t *p_ml = vlc_custom_create( VLC_OBJECT( p_libvlc ),
                                                  sizeof( *p_ml ), "medialibrary" );
    if ( unlikely( p_ml == NULL ) )
        return NULL;
    vlc_mutex_init( &p_ml->lock );
    vlc_list_init( &p_ml->cbs );
    p_ml->m.cbs = &callbacks;
    p_ml->m.p_module = module_need( &p_ml->m, "medialibrary", NULL, false );
    if ( p_ml->m.p_module == NULL )
    {
        vlc_object_release( &p_ml->m );
        vlc_mutex_destroy( &p_ml->lock );
        return NULL;
    }
    return p_ml;
}

void libvlc_MlRelease( vlc_medialibrary_t* p_ml )
{
    assert( p_ml != NULL );
    module_unneed( &p_ml->m, p_ml->m.p_module );
    assert( vlc_list_is_empty( &p_ml->cbs ) );
    vlc_mutex_destroy( &p_ml->lock );
    vlc_object_release( &p_ml->m );
}

#undef vlc_ml_instance_get
vlc_medialibrary_t* vlc_ml_instance_get( vlc_object_t* p_obj )
{
    libvlc_priv_t* p_priv = libvlc_priv( p_obj->obj.libvlc );
    return p_priv->p_media_library;
}

static void vlc_ml_show_release_inner( vlc_ml_show_t* p_show )
{
    free( p_show->psz_artwork_mrl );
    free( p_show->psz_name );
    free( p_show->psz_summary );
    free( p_show->psz_tvdb_id );
}

void vlc_ml_show_release( vlc_ml_show_t* p_show )
{
    if ( p_show == NULL )
        return;
    vlc_ml_show_release_inner( p_show );
    free( p_show );
}

static void vlc_ml_media_release_tracks_inner( vlc_ml_media_track_list_t* p_tracks )
{
    if ( p_tracks == NULL )
        return;
    for ( size_t i = 0; i < p_tracks->i_nb_items; ++i )
    {
        vlc_ml_media_track_t* p_track = &p_tracks->p_items[i];
        free( p_track->psz_codec );
        free( p_track->psz_language );
        free( p_track->psz_description );
    }
    free( p_tracks );
}

static void vlc_ml_media_release_inner( vlc_ml_media_t* p_media )
{
    vlc_ml_file_list_release( p_media->p_files );
    vlc_ml_media_release_tracks_inner( p_media->p_tracks );
    free( p_media->psz_title );
    free( p_media->psz_artwork_mrl );
    switch( p_media->i_subtype )
    {
        case VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK:
            break;
        case VLC_ML_MEDIA_SUBTYPE_SHOW_EPISODE:
            free( p_media->show_episode.psz_summary );
            free( p_media->show_episode.psz_tvdb_id );
            break;
        case VLC_ML_MEDIA_SUBTYPE_MOVIE:
            free( p_media->movie.psz_summary );
            free( p_media->movie.psz_imdb_id );
            break;
        default:
            break;
    }
}

static void vlc_ml_artist_release_inner( vlc_ml_artist_t* p_artist )
{
    free( p_artist->psz_artwork_mrl );
    free( p_artist->psz_name );
    free( p_artist->psz_shortbio );
    free( p_artist->psz_mb_id );
}

void vlc_ml_artist_release( vlc_ml_artist_t* p_artist )
{
    if ( p_artist == NULL )
        return;
    vlc_ml_artist_release_inner( p_artist );
    free( p_artist );
}

static void vlc_ml_album_release_inner( vlc_ml_album_t* p_album )
{
    free( p_album->psz_artist );
    free( p_album->psz_artwork_mrl );
    free( p_album->psz_summary );
    free( p_album->psz_title );
}

void vlc_ml_album_release( vlc_ml_album_t* p_album )
{
    if ( p_album == NULL )
        return;
    vlc_ml_album_release_inner( p_album );
    free( p_album );
}

void vlc_ml_genre_release( vlc_ml_genre_t* p_genre )
{
    if ( p_genre == NULL )
        return;
    free( p_genre->psz_name );
    free( p_genre );
}

static void vlc_ml_playlist_release_inner( vlc_ml_playlist_t* p_playlist )
{
    free( p_playlist->psz_artwork_mrl );
    free( p_playlist->psz_name );
}

void vlc_ml_playlist_release( vlc_ml_playlist_t* p_playlist )
{
    if ( p_playlist == NULL )
        return;
    vlc_ml_playlist_release_inner( p_playlist );
    free( p_playlist );
}

/* Lists release */

void vlc_ml_media_release( vlc_ml_media_t* p_media )
{
    if ( p_media == NULL )
        return;
    vlc_ml_media_release_inner( p_media );
    free( p_media );
}

void vlc_ml_label_list_release( vlc_ml_label_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        free( p_list->p_items[i].psz_name );
    free( p_list );
}

void vlc_ml_file_list_release( vlc_ml_file_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        free( p_list->p_items[i].psz_mrl );
    free( p_list );
}

void vlc_ml_artist_list_release( vlc_ml_artist_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        vlc_ml_artist_release_inner( &p_list->p_items[i] );
    free( p_list );
}


void vlc_ml_media_list_release( vlc_ml_media_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        vlc_ml_media_release_inner( &p_list->p_items[i] );
    free( p_list );
}

void vlc_ml_album_list_release( vlc_ml_album_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        vlc_ml_album_release_inner( &p_list->p_items[i] );
    free( p_list );
}

void vlc_ml_show_list_release( vlc_ml_show_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        vlc_ml_show_release_inner( &p_list->p_items[i] );
    free( p_list );
}

void vlc_ml_genre_list_release( vlc_ml_genre_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        free( p_list->p_items[i].psz_name );
    free( p_list );
}

void vlc_ml_playlist_list_release( vlc_ml_playlist_list_t* p_list )
{
    if ( p_list == NULL )
        return;
    for ( size_t i = 0; i < p_list->i_nb_items; ++i )
        vlc_ml_playlist_release_inner( &p_list->p_items[i] );
    free( p_list );
}

void* vlc_ml_get( vlc_medialibrary_t* p_ml, int i_query, int64_t i_id )
{
    assert( p_ml != NULL );
    return p_ml->m.pf_get( &p_ml->m, i_query, i_id );
}

int vlc_ml_control( vlc_medialibrary_t* p_ml, int i_query, ... )
{
    va_list args;
    va_start( args, i_query );
    int i_res = p_ml->m.pf_control( &p_ml->m, i_query, args );
    va_end( args );
    return i_res;
}

int vlc_ml_list( vlc_medialibrary_t* p_ml, int i_query,
                             const vlc_ml_query_params_t* p_params, ... )
{
    va_list args;
    va_start( args, p_params );
    int i_res = p_ml->m.pf_list( &p_ml->m, i_query, p_params, args );
    va_end( args );
    return i_res;
}