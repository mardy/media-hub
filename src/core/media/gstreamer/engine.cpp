/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voß <thomas.voss@canonical.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "bus.h"
#include "engine.h"
#include "meta_data_extractor.h"
#include "playbin.h"

#include <cassert>

namespace media = core::ubuntu::media;

using namespace std;

namespace gstreamer
{
struct Init
{
    Init()
    {
        gst_init(nullptr, nullptr);
    }

    ~Init()
    {
        gst_deinit();
    }
} init;
}

struct gstreamer::Engine::Private
{
    void on_playbin_state_changed(
            const gstreamer::Bus::Message::Detail::StateChanged& state_change)
    {
        (void) state_change;
    }

    void on_tag_available(const gstreamer::Bus::Message::Detail::Tag& tag)
    {
        media::Track::MetaData md;

        gst_tag_list_foreach(
                    tag.tag_list,
                    [](const GstTagList *list,
                    const gchar* tag,
                    gpointer user_data)
        {
            (void) list;

            static const std::map<std::string, std::string> gstreamer_to_mpris_tag_lut =
            {
                {GST_TAG_ALBUM, media::Engine::Xesam::album()},
                {GST_TAG_ALBUM_ARTIST, media::Engine::Xesam::album_artist()},
                {GST_TAG_ARTIST, media::Engine::Xesam::artist()},
                {GST_TAG_LYRICS, media::Engine::Xesam::as_text()},
                {GST_TAG_COMMENT, media::Engine::Xesam::comment()},
                {GST_TAG_COMPOSER, media::Engine::Xesam::composer()},
                {GST_TAG_DATE, media::Engine::Xesam::content_created()},
                {GST_TAG_ALBUM_VOLUME_NUMBER, media::Engine::Xesam::disc_number()},
                {GST_TAG_GENRE, media::Engine::Xesam::genre()},
                {GST_TAG_TITLE, media::Engine::Xesam::title()},
                {GST_TAG_TRACK_NUMBER, media::Engine::Xesam::track_number()},
                {GST_TAG_USER_RATING, media::Engine::Xesam::user_rating()}
            };

            auto md = static_cast<media::Track::MetaData*>(user_data);
            std::stringstream ss;

            switch(gst_tag_get_type(tag))
            {
            case G_TYPE_BOOLEAN:
            {
                gboolean value;
                if (gst_tag_list_get_boolean(list, tag, &value))
                    ss << value;
                break;
            }
            case G_TYPE_INT:
            {
                gint value;
                if (gst_tag_list_get_int(list, tag, &value))
                    ss << value;
                break;
            }
            case G_TYPE_UINT:
            {
                guint value;
                if (gst_tag_list_get_uint(list, tag, &value))
                    ss << value;
                break;
            }
            case G_TYPE_INT64:
            {
                gint64 value;
                if (gst_tag_list_get_int64(list, tag, &value))
                    ss << value;
                break;
            }
            case G_TYPE_UINT64:
            {
                guint64 value;
                if (gst_tag_list_get_uint64(list, tag, &value))
                    ss << value;
                break;
            }
            case G_TYPE_FLOAT:
            {
                gfloat value;
                if (gst_tag_list_get_float(list, tag, &value))
                    ss << value;
                break;
            }
            case G_TYPE_DOUBLE:
            {
                double value;
                if (gst_tag_list_get_double(list, tag, &value))
                    ss << value;
                break;
            }
            case G_TYPE_STRING:
            {
                gchar* value;
                if (gst_tag_list_get_string(list, tag, &value))
                {
                    ss << value;
                    g_free(value);
                }
                break;
            }
            default:
                break;
            }

            (*md).set(
                        (gstreamer_to_mpris_tag_lut.count(tag) > 0 ? gstreamer_to_mpris_tag_lut.at(tag) : tag),
                        ss.str());
        },
        &md);

        track_meta_data.set(std::make_tuple(playbin.uri(), md));
    }

    void on_volume_changed(const media::Engine::Volume& new_volume)
    {
        playbin.set_volume(new_volume.value);
    }

    void on_about_to_finish()
    {
        state = Engine::State::ready;
        about_to_finish();
    }

    void on_seeked_to(uint64_t value)
    {
        seeked_to(value);
    }

    void on_end_of_stream()
    {
        end_of_stream();
    }

    Private()
        : meta_data_extractor(new gstreamer::MetaDataExtractor()),
          volume(media::Engine::Volume(1.)),
          is_video_source(false),
          is_audio_source(false),
          about_to_finish_connection(
              playbin.signals.about_to_finish.connect(
                  std::bind(
                      &Private::on_about_to_finish,
                      this))),
          on_state_changed_connection(
              playbin.signals.on_state_changed.connect(
                  std::bind(
                      &Private::on_playbin_state_changed,
                      this,
                      std::placeholders::_1))),
          on_tag_available_connection(
              playbin.signals.on_tag_available.connect(
                  std::bind(
                      &Private::on_tag_available,
                      this,
                      std::placeholders::_1))),
          on_volume_changed_connection(
              volume.changed().connect(
                  std::bind(
                      &Private::on_volume_changed,
                      this,
                      std::placeholders::_1))),
          on_seeked_to_connection(
              playbin.signals.on_seeked_to.connect(
                  std::bind(
                      &Private::on_seeked_to,
                      this,
                      std::placeholders::_1))),
          on_end_of_stream_connection(
              playbin.signals.on_end_of_stream.connect(
                  std::bind(
                      &Private::on_end_of_stream,
                      this)))
    {
    }

    std::shared_ptr<Engine::MetaDataExtractor> meta_data_extractor;
    core::Property<Engine::State> state;
    core::Property<std::tuple<media::Track::UriType, media::Track::MetaData>> track_meta_data;
    core::Property<uint64_t> position;
    core::Property<uint64_t> duration;
    core::Property<media::Engine::Volume> volume;
    core::Property<bool> is_video_source;
    core::Property<bool> is_audio_source;
    gstreamer::Playbin playbin;
    core::ScopedConnection about_to_finish_connection;
    core::ScopedConnection on_state_changed_connection;
    core::ScopedConnection on_tag_available_connection;
    core::ScopedConnection on_volume_changed_connection;
    core::ScopedConnection on_seeked_to_connection;
    core::ScopedConnection on_end_of_stream_connection;

    core::Signal<void> about_to_finish;
    core::Signal<uint64_t> seeked_to;
    core::Signal<void> end_of_stream;
    core::Signal<media::Player::PlaybackStatus> playback_status_changed;
};

gstreamer::Engine::Engine() : d(new Private{})
{
    cout << "Creating a new Engine instance in " << __PRETTY_FUNCTION__ << endl;
    d->state = media::Engine::State::ready;
}

gstreamer::Engine::~Engine()
{
    stop();
}

const std::shared_ptr<media::Engine::MetaDataExtractor>& gstreamer::Engine::meta_data_extractor() const
{
    return d->meta_data_extractor;
}

const core::Property<media::Engine::State>& gstreamer::Engine::state() const
{
    return d->state;
}

bool gstreamer::Engine::open_resource_for_uri(const media::Track::UriType& uri)
{
    d->playbin.set_uri(uri);
    return true;
}

bool gstreamer::Engine::open_resource_for_uri(const media::Track::UriType& uri, const core::ubuntu::media::Player::HeadersType& headers)
{
    d->playbin.set_uri(uri, headers);
    return true;
}

void gstreamer::Engine::create_video_sink(uint32_t texture_id)
{
    d->playbin.create_video_sink(texture_id);
}

bool gstreamer::Engine::play()
{
    auto result = d->playbin.set_state_and_wait(GST_STATE_PLAYING);

    if (result)
    {
        d->state = media::Engine::State::playing;
        d->playback_status_changed(media::Player::PlaybackStatus::playing);
    }

    cout << "Engine: " << this << endl;

    return result;
}

bool gstreamer::Engine::stop()
{
    auto result = d->playbin.set_state_and_wait(GST_STATE_NULL);

    if (result)
    {
        d->state = media::Engine::State::stopped;
        d->playback_status_changed(media::Player::PlaybackStatus::stopped);
    }

    return result;
}

bool gstreamer::Engine::pause()
{
    auto result = d->playbin.set_state_and_wait(GST_STATE_PAUSED);

    if (result)
    {
        d->state = media::Engine::State::paused;
        cout << "pause" << endl;
        d->playback_status_changed(media::Player::PlaybackStatus::paused);
    }

    return result;
}

bool gstreamer::Engine::seek_to(const std::chrono::microseconds& ts)
{
    return d->playbin.seek(ts);
}

const core::Property<bool>& gstreamer::Engine::is_video_source() const
{
    gstreamer::Playbin::MediaFileType type = d->playbin.media_file_type();
    if (type == gstreamer::Playbin::MediaFileType::MEDIA_FILE_TYPE_VIDEO)
        d->is_video_source.set(true);
    else
        d->is_video_source.set(false);

    return d->is_video_source;
}

const core::Property<bool>& gstreamer::Engine::is_audio_source() const
{
    gstreamer::Playbin::MediaFileType type = d->playbin.media_file_type();
    if (type == gstreamer::Playbin::MediaFileType::MEDIA_FILE_TYPE_AUDIO)
        d->is_audio_source.set(true);
    else
        d->is_audio_source.set(false);

    return d->is_audio_source;
}

const core::Property<uint64_t>& gstreamer::Engine::position() const
{
    d->position.set(d->playbin.position());
    return d->position;
}

const core::Property<uint64_t>& gstreamer::Engine::duration() const
{
    d->duration.set(d->playbin.duration());
    return d->duration;
}

const core::Property<core::ubuntu::media::Engine::Volume>& gstreamer::Engine::volume() const
{
    return d->volume;
}

core::Property<core::ubuntu::media::Engine::Volume>& gstreamer::Engine::volume()
{
    return d->volume;
}

const core::Property<std::tuple<media::Track::UriType, media::Track::MetaData>>&
gstreamer::Engine::track_meta_data() const
{
    return d->track_meta_data;
}

const core::Signal<void>& gstreamer::Engine::about_to_finish_signal() const
{
    return d->about_to_finish;
}

const core::Signal<uint64_t>& gstreamer::Engine::seeked_to_signal() const
{
    return d->seeked_to;
}

const core::Signal<void>& gstreamer::Engine::end_of_stream_signal() const
{
    return d->end_of_stream;
}

const core::Signal<media::Player::PlaybackStatus>& gstreamer::Engine::playback_status_changed_signal() const
{
    return d->playback_status_changed;
}
