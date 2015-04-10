/*
 * Copyright © 2013-2014 Canonical Ltd.
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
 *              Jim Hodapp <jim.hodapp@canonical.com>
 */

#include "service_skeleton.h"

#include "mpris/media_player2.h"
#include "mpris/metadata.h"
#include "mpris/player.h"
#include "mpris/playlists.h"
#include "mpris/service.h"

#include "player_configuration.h"
#include "the_session_bus.h"
#include "xesam.h"

#include <core/dbus/message.h>
#include <core/dbus/object.h>
#include <core/dbus/types/object_path.h>

#include <core/posix/this_process.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <map>
#include <regex>
#include <sstream>

namespace dbus = core::dbus;
namespace media = core::ubuntu::media;

namespace
{
core::Signal<void> the_empty_signal;
}

struct media::ServiceSkeleton::Private
{
    Private(media::ServiceSkeleton* impl, const ServiceSkeleton::Configuration& config)
        : request_context_resolver(media::apparmor::ubuntu::make_platform_default_request_context_resolver(config.external_services)),
          impl(impl),
          object(impl->access_service()->add_object_for_path(
                     dbus::traits::Service<media::Service>::object_path())),          
          configuration(config),
          exported(impl->access_bus(), config.cover_art_resolver)
    {
        object->install_method_handler<mpris::Service::CreateSession>(
                    std::bind(
                        &Private::handle_create_session,
                        this,
                        std::placeholders::_1));
        object->install_method_handler<mpris::Service::DetachSession>(
                    std::bind(
                        &Private::handle_detach_session,
                        this,
                        std::placeholders::_1));
        object->install_method_handler<mpris::Service::ReattachSession>(
                    std::bind(
                        &Private::handle_reattach_session,
                        this,
                        std::placeholders::_1));
        object->install_method_handler<mpris::Service::DestroySession>(
                    std::bind(
                        &Private::handle_destroy_session,
                        this,
                        std::placeholders::_1));
        object->install_method_handler<mpris::Service::CreateFixedSession>(
                    std::bind(
                        &Private::handle_create_fixed_session,
                        this,
                        std::placeholders::_1));
        object->install_method_handler<mpris::Service::ResumeSession>(
                    std::bind(
                        &Private::handle_resume_session,
                        this,
                        std::placeholders::_1));
        object->install_method_handler<mpris::Service::PauseOtherSessions>(
                    std::bind(
                        &Private::handle_pause_other_sessions,
                        this,
                        std::placeholders::_1));
    }

    std::tuple<std::string, media::Player::PlayerKey, std::string> create_session_info()
    {
        static unsigned int session_counter = 0;

        unsigned int current_session = session_counter++;
        boost::uuids::uuid uuid = gen();

        std::stringstream ss;
        ss << "/core/ubuntu/media/Service/sessions/" << current_session;

        return std::make_tuple(ss.str(), media::Player::PlayerKey(current_session), to_string(uuid));
    }

    void handle_create_session(const core::dbus::Message::Ptr& msg)
    {
        auto session_info = create_session_info();

        dbus::types::ObjectPath op{std::get<0>(session_info)};
        media::Player::PlayerKey key{std::get<1>(session_info)};
        std::string uuid{std::get<2>(session_info)};

        media::Player::Configuration config
        {
            key,
            impl->access_bus(),
            impl->access_service()->add_object_for_path(op)
        };

        std::cout << "Session created by request of: " << msg->sender() << ", uuid: " << uuid << ", path:" << op << std::endl;

        try
        {
            configuration.player_store->add_player_for_key(key, impl->create_session(config));
            uuid_player_map.insert(std::make_pair(uuid, key));

            request_context_resolver->resolve_context_for_dbus_name_async(msg->sender(), [this, key, msg](const media::apparmor::ubuntu::Context& context)
            {
                fprintf(stderr, "%s():%d -- app_name='%s', attached\n", __func__, __LINE__, context.str().c_str());
                player_owner_map.insert(std::make_pair(key, std::make_tuple(context.str(), true, msg->sender())));
            });
            
            auto reply = dbus::Message::make_method_return(msg);
            reply->writer() << op;

            impl->access_bus()->send(reply);
        } catch(const std::runtime_error& e)
        {
            auto reply = dbus::Message::make_error(
                        msg,
                        mpris::Service::Errors::CreatingSession::name(),
                        e.what());
            impl->access_bus()->send(reply);
        }
    }

    void handle_detach_session(const core::dbus::Message::Ptr& msg)
    {
        try
        {
            std::string uuid;
            msg->reader() >> uuid;

            auto key = uuid_player_map.at(uuid);

            if (player_owner_map.count(key) != 0) {
                auto info = player_owner_map.at(key);
                if (std::get<1>(info) && (std::get<2>(info) == msg->sender())) { // Player is attached
                    std::get<1>(info) = false; // Detached
                    std::get<2>(info).clear(); // Clear registered sender/peer
                }
            }
            
            auto reply = dbus::Message::make_method_return(msg);
            impl->access_bus()->send(reply);

        } catch(const std::runtime_error& e)
        {
            auto reply = dbus::Message::make_error(
                        msg,
                        mpris::Service::Errors::DetachingSession::name(),
                        e.what());
            impl->access_bus()->send(reply);
        }
    }

    void handle_reattach_session(const core::dbus::Message::Ptr& msg)
    {
        try
        {
            std::string uuid;
            msg->reader() >> uuid;

            if (uuid_player_map.count(uuid) != 0) {
                auto key = uuid_player_map.at(uuid);
                if (not configuration.player_store->has_player_for_key(key)) {
                    auto reply = dbus::Message::make_error(
                                msg,
                                mpris::Service::Errors::ReattachingSession::name(),
                                "Unable to locate player session");
                    impl->access_bus()->send(reply);
                    return;
                }
                std::stringstream ss;
                ss << "/core/ubuntu/media/Service/sessions/" << key;
                dbus::types::ObjectPath op{ss.str()};

                request_context_resolver->resolve_context_for_dbus_name_async(msg->sender(), [this, msg, key, op](const media::apparmor::ubuntu::Context& context)
                {
                    auto info = player_owner_map.at(key);
                    fprintf(stderr, "%s():%d -- reattach app_name='%s', info='%s', '%s'\n", __func__, __LINE__, context.str().c_str(), std::get<0>(info).c_str(), std::get<2>(info).c_str());
                    if (std::get<0>(info) == context.str()) {
                        std::get<1>(info) = true; // Set to Attached
                        std::get<2>(info) = msg->sender(); // Register new owner

                        // Signal player reconnection
                        auto player = configuration.player_store->player_for_key(key);
                        player->reconnect();

                        auto reply = dbus::Message::make_method_return(msg);
                        reply->writer() << op;

                        impl->access_bus()->send(reply);
                    }
                    else {
                        auto reply = dbus::Message::make_error(
                                    msg,
                                    mpris::Service::Errors::ReattachingSession::name(),
                                    "Invalid permissions for the requested session");
                        impl->access_bus()->send(reply);
                        return;
                    }
                });
            }
            else {
               auto reply = dbus::Message::make_error(
                           msg,
                           mpris::Service::Errors::ReattachingSession::name(),
                           "Invalid session");
               impl->access_bus()->send(reply);
               return;
            }
        } catch(const std::runtime_error& e)
        {
            auto reply = dbus::Message::make_error(
                        msg,
                        mpris::Service::Errors::ReattachingSession::name(),
                        e.what());
            impl->access_bus()->send(reply);
        }
    }

    void handle_destroy_session(const core::dbus::Message::Ptr& msg)
    {
     
        try
        {
            std::string uuid;
            msg->reader() >> uuid;

            if (uuid_player_map.count(uuid) != 0) {
                auto key = uuid_player_map.at(uuid);
                if (not configuration.player_store->has_player_for_key(key)) {
                    auto reply = dbus::Message::make_error(
                                msg,
                                mpris::Service::Errors::DestroyingSession::name(),
                                "Unable to locate player session");
                    impl->access_bus()->send(reply);
                    return;
                }

                // Remove control entries from the map, at this point
                // the session is no longer usable.
                uuid_player_map.erase(uuid);

                request_context_resolver->resolve_context_for_dbus_name_async(msg->sender(), [this, msg, key](const media::apparmor::ubuntu::Context& context)
                {
                    auto info = player_owner_map.at(key);
                    fprintf(stderr, "%s():%d -- Destroying app_name='%s', info='%s', '%s'\n", __func__, __LINE__, context.str().c_str(), std::get<0>(info).c_str(), std::get<2>(info).c_str());
                    if (std::get<0>(info) == context.str()) {
                        player_owner_map.erase(key);

                        // Signal player reconnection
                        auto player = configuration.player_store->player_for_key(key);
                        player->lifetime().set(media::Player::Lifetime::normal);

                        auto reply = dbus::Message::make_method_return(msg);
                        impl->access_bus()->send(reply);
                    }
                    else {
                        auto reply = dbus::Message::make_error(
                                    msg,
                                    mpris::Service::Errors::DestroyingSession::name(),
                                    "Invalid permissions for the requested session");
                        impl->access_bus()->send(reply);
                        return;
                    }
                });
            }
            else {
               auto reply = dbus::Message::make_error(
                           msg,
                           mpris::Service::Errors::DestroyingSession::name(),
                           "Invalid session");
               impl->access_bus()->send(reply);
               return;
            }
        } catch(const std::runtime_error& e)
        {
            auto reply = dbus::Message::make_error(
                        msg,
                        mpris::Service::Errors::DestroyingSession::name(),
                        e.what());
            impl->access_bus()->send(reply);
        }
    }

    void handle_create_fixed_session(const core::dbus::Message::Ptr& msg)
    {
        try
        {
            std::string name;
            msg->reader() >> name;

            if (named_player_map.count(name) == 0) {
                // Create new session
                auto  session_info = create_session_info();

                dbus::types::ObjectPath op{std::get<0>(session_info)};
                media::Player::PlayerKey key{std::get<1>(session_info)};

                media::Player::Configuration config
                {
                    key,
                    impl->access_bus(),
                    impl->access_service()->add_object_for_path(op)
                };

                auto session = impl->create_session(config);
                session->lifetime().set(media::Player::Lifetime::resumable);

                configuration.player_store->add_player_for_key(key, session);

                named_player_map.insert(std::make_pair(name, key));

                auto reply = dbus::Message::make_method_return(msg);
                reply->writer() << op;

                impl->access_bus()->send(reply);
            }
            else {
                // Resume previous session
                auto key = named_player_map.at(name);
                if (not configuration.player_store->has_player_for_key(key)) {
                    auto reply = dbus::Message::make_error(
                                msg,
                                mpris::Service::Errors::CreatingFixedSession::name(),
                                "Unable to locate player session");
                    impl->access_bus()->send(reply);
                    return;
                }

                std::stringstream ss;
                ss << "/core/ubuntu/media/Service/sessions/" << key;
                dbus::types::ObjectPath op{ss.str()};

                auto reply = dbus::Message::make_method_return(msg);
                reply->writer() << op;

                impl->access_bus()->send(reply);
            }
        } catch(const std::runtime_error& e)
        {
            auto reply = dbus::Message::make_error(
                        msg,
                        mpris::Service::Errors::CreatingSession::name(),
                        e.what());
            impl->access_bus()->send(reply);
        }
    }

    void handle_resume_session(const core::dbus::Message::Ptr& msg)
    {
        try
        {
            Player::PlayerKey key;
            msg->reader() >> key;

            if (not configuration.player_store->has_player_for_key(key)) {
                auto reply = dbus::Message::make_error(
                            msg,
                            mpris::Service::Errors::ResumingSession::name(),
                            "Unable to locate player session");
                impl->access_bus()->send(reply);
                return;
            }

            std::stringstream ss;
            ss << "/core/ubuntu/media/Service/sessions/" << key;
            dbus::types::ObjectPath op{ss.str()};

            auto reply = dbus::Message::make_method_return(msg);
            reply->writer() << op;

            impl->access_bus()->send(reply);
        } catch(const std::runtime_error& e)
        {
            auto reply = dbus::Message::make_error(
                        msg,
                        mpris::Service::Errors::CreatingSession::name(),
                        e.what());
            impl->access_bus()->send(reply);
        }
    }

    void handle_pause_other_sessions(const core::dbus::Message::Ptr& msg)
    {
        std::cout << __PRETTY_FUNCTION__ << std::endl;
        Player::PlayerKey key;
        msg->reader() >> key;
        impl->pause_other_sessions(key);

        auto reply = dbus::Message::make_method_return(msg);
        impl->access_bus()->send(reply);
    }

    media::apparmor::ubuntu::RequestContextResolver::Ptr request_context_resolver;
    media::ServiceSkeleton* impl;
    dbus::Object::Ptr object;

    // We remember all our creation time arguments.
    ServiceSkeleton::Configuration configuration;
    // We map named/fixed player instances to their respective keys.
    std::map<std::string, media::Player::PlayerKey> named_player_map;
    // We map UUIDs to their respective keys.
    std::map<std::string, media::Player::PlayerKey> uuid_player_map;
    // We keep a list of keys and their respective owners and states.
    // value: (owner context, attached state, attached dbus name)
    std::map<media::Player::PlayerKey, std::tuple<std::string, bool, std::string>> player_owner_map;
    
    boost::uuids::random_generator gen;
     
    // We expose the entire service as an MPRIS player.
    struct Exported
    {
        static mpris::MediaPlayer2::Skeleton::Configuration::Defaults media_player_defaults()
        {
            mpris::MediaPlayer2::Skeleton::Configuration::Defaults defaults;
            // TODO(tvoss): These three elements really should be configurable.
            defaults.identity = "core::media::Hub";
            defaults.desktop_entry = "mediaplayer-app";
            defaults.supported_mime_types = {"audio/mpeg3"};

            return defaults;
        }

        static mpris::Player::Skeleton::Configuration::Defaults player_defaults()
        {
            mpris::Player::Skeleton::Configuration::Defaults defaults;

            // Disabled as track list is not fully implemented yet.
            defaults.can_go_next = false;
            // Disabled as track list is not fully implemented yet.
            defaults.can_go_previous = false;

            return defaults;
        }

        static std::string service_name()
        {
            static const bool export_to_indicator_sound_via_mpris
            {
                core::posix::this_process::env::get("UBUNTU_MEDIA_HUB_EXPORT_TO_INDICATOR_VIA_MPRIS", "0") == "1"
            };

            return export_to_indicator_sound_via_mpris ? "org.mpris.MediaPlayer2.MediaHub" :
                                                         "hidden.org.mpris.MediaPlayer2.MediaHub";
        }

        explicit Exported(const dbus::Bus::Ptr& bus, const media::CoverArtResolver& cover_art_resolver)
            : bus{bus},
              service{dbus::Service::add_service(bus, service_name())},
              object{service->add_object_for_path(dbus::types::ObjectPath{"/org/mpris/MediaPlayer2"})},
              media_player{mpris::MediaPlayer2::Skeleton::Configuration{bus, object, media_player_defaults()}},
              player{mpris::Player::Skeleton::Configuration{bus, object, player_defaults()}},
              playlists{mpris::Playlists::Skeleton::Configuration{bus, object, mpris::Playlists::Skeleton::Configuration::Defaults{}}},
              cover_art_resolver{cover_art_resolver}
        {
            object->install_method_handler<core::dbus::interfaces::Properties::GetAll>([this](const core::dbus::Message::Ptr& msg)
            {
                // Extract the interface
                std::string itf; msg->reader() >> itf;
                core::dbus::Message::Ptr reply = core::dbus::Message::make_method_return(msg);

                if (itf == mpris::Player::name())
                    reply->writer() << player.get_all_properties();
                else if (itf == mpris::MediaPlayer2::name())
                    reply->writer() << media_player.get_all_properties();
                else if (itf == mpris::Playlists::name())
                    reply->writer() << playlists.get_all_properties();

                Exported::bus->send(reply);
            });

            // Setup method handlers for mpris::Player methods.
            auto next = [this](const core::dbus::Message::Ptr& msg)
            {
                auto sp = current_player.lock();

                if (sp)
                    sp->next();

                Exported::bus->send(core::dbus::Message::make_method_return(msg));
            };
            object->install_method_handler<mpris::Player::Next>(next);

            auto previous = [this](const core::dbus::Message::Ptr& msg)
            {
                auto sp = current_player.lock();

                if (sp)
                    sp->previous();

                Exported::bus->send(core::dbus::Message::make_method_return(msg));
            };
            object->install_method_handler<mpris::Player::Previous>(previous);

            auto pause = [this](const core::dbus::Message::Ptr& msg)
            {
                auto sp = current_player.lock();

                if (sp)
                    sp->pause();

                Exported::bus->send(core::dbus::Message::make_method_return(msg));
            };
            object->install_method_handler<mpris::Player::Pause>(pause);

            auto stop = [this](const core::dbus::Message::Ptr& msg)
            {
                auto sp = current_player.lock();

                if (sp)
                    sp->stop();

                Exported::bus->send(core::dbus::Message::make_method_return(msg));
            };
            object->install_method_handler<mpris::Player::Stop>(stop);

            auto play = [this](const core::dbus::Message::Ptr& msg)
            {
                auto sp = current_player.lock();

                if (sp)
                    sp->play();

                Exported::bus->send(core::dbus::Message::make_method_return(msg));
            };
            object->install_method_handler<mpris::Player::Play>(play);

            auto play_pause = [this](const core::dbus::Message::Ptr& msg)
            {
                auto sp = current_player.lock();

                if (sp)
                {
                    if (sp->playback_status() == media::Player::PlaybackStatus::playing)
                        sp->pause();
                    else if (sp->playback_status() != media::Player::PlaybackStatus::null)
                        sp->play();
                }

                Exported::bus->send(core::dbus::Message::make_method_return(msg));
            };
            object->install_method_handler<mpris::Player::PlayPause>(play_pause);
        }

        void set_current_player(const std::shared_ptr<media::Player>& cp)
        {
            unset_current_player();

            // We will not keep the object alive.
            current_player = cp;

            // And announce that we can be controlled again.
            player.properties.can_control->set(false);

            // We wire up player state changes
            connections.seeked_to = cp->seeked_to().connect([this](std::uint64_t position)
            {
                player.signals.seeked_to->emit(position);
            });

            connections.duration_changed = cp->duration().changed().connect([this](std::uint64_t duration)
            {
                player.properties.duration->set(duration);
            });

            connections.position_changed = cp->position().changed().connect([this](std::uint64_t position)
            {
                player.properties.position->set(position);
            });

            connections.playback_status_changed = cp->playback_status().changed().connect([this](core::ubuntu::media::Player::PlaybackStatus status)
            {
                player.properties.playback_status->set(mpris::Player::PlaybackStatus::from(status));
            });

            connections.loop_status_changed = cp->loop_status().changed().connect([this](core::ubuntu::media::Player::LoopStatus status)
            {
                player.properties.loop_status->set(mpris::Player::LoopStatus::from(status));
            });

            connections.meta_data_changed = cp->meta_data_for_current_track().changed().connect([this](const core::ubuntu::media::Track::MetaData& md)
            {
                mpris::Player::Dictionary dict;

                bool has_title = md.count(xesam::Title::name) > 0;
                bool has_album_name = md.count(xesam::Album::name) > 0;
                bool has_artist_name = md.count(xesam::Artist::name) > 0;

                if (has_title)
                    dict[xesam::Title::name] = dbus::types::Variant::encode(md.get(xesam::Title::name));
                if (has_album_name)
                    dict[xesam::Album::name] = dbus::types::Variant::encode(md.get(xesam::Album::name));
                if (has_artist_name)
                    dict[xesam::Artist::name] = dbus::types::Variant::encode(md.get(xesam::Artist::name));

                dict[mpris::metadata::ArtUrl::name] = dbus::types::Variant::encode(
                            cover_art_resolver(
                                has_title ? md.get(xesam::Title::name) : "",
                                has_album_name ? md.get(xesam::Album::name) : "",
                                has_artist_name ? md.get(xesam::Artist::name) : ""));

                mpris::Player::Dictionary wrap;
                wrap[mpris::Player::Properties::Metadata::name()] = dbus::types::Variant::encode(dict);

                player.signals.properties_changed->emit(
                            std::make_tuple(
                                dbus::traits::Service<mpris::Player::Properties::Metadata::Interface>::interface_name(),
                                wrap,
                                std::vector<std::string>()));
            });
        }

        void unset_current_player()
        {
            current_player.reset();

            // We disconnect all previous event connections.
            connections.seeked_to.disconnect();
            connections.duration_changed.disconnect();
            connections.position_changed.disconnect();
            connections.playback_status_changed.disconnect();
            connections.loop_status_changed.disconnect();
            connections.meta_data_changed.disconnect();

            // And announce that we cannot be controlled anymore.
            player.properties.can_control->set(false);
        }

        void unset_if_current(const std::shared_ptr<media::Player>& cp)
        {
            if (cp == current_player.lock())
                unset_current_player();
        }

        dbus::Bus::Ptr bus;
        dbus::Service::Ptr service;
        dbus::Object::Ptr object;

        mpris::MediaPlayer2::Skeleton media_player;
        mpris::Player::Skeleton player;
        mpris::Playlists::Skeleton playlists;

        // The CoverArtResolver used by the exported player.
        media::CoverArtResolver cover_art_resolver;
        // The actual player instance.
        std::weak_ptr<media::Player> current_player;
        // We track event connections.
        struct
        {
            core::Connection seeked_to
            {
                the_empty_signal.connect([](){})
            };
            core::Connection duration_changed
            {
                the_empty_signal.connect([](){})
            };
            core::Connection position_changed
            {
                the_empty_signal.connect([](){})
            };
            core::Connection playback_status_changed
            {
                the_empty_signal.connect([](){})
            };
            core::Connection loop_status_changed
            {
                the_empty_signal.connect([](){})
            };
            core::Connection meta_data_changed
            {
                the_empty_signal.connect([](){})
            };
        } connections;
    } exported;
};

media::ServiceSkeleton::ServiceSkeleton(const Configuration& configuration)
    : dbus::Skeleton<media::Service>(the_session_bus()),
      d(new Private(this, configuration))
{
}

media::ServiceSkeleton::~ServiceSkeleton()
{
}

std::shared_ptr<media::Player> media::ServiceSkeleton::create_session(const media::Player::Configuration& config)
{
    return d->configuration.impl->create_session(config);
}

std::shared_ptr<media::Player> media::ServiceSkeleton::create_fixed_session(const std::string& name, const media::Player::Configuration&config)
{
    return d->configuration.impl->create_fixed_session(name, config);
}

std::shared_ptr<media::Player> media::ServiceSkeleton::resume_session(media::Player::PlayerKey key)
{
    return d->configuration.impl->resume_session(key);
}

void media::ServiceSkeleton::pause_other_sessions(media::Player::PlayerKey key)
{
    d->configuration.impl->pause_other_sessions(key);
}

void media::ServiceSkeleton::run()
{
    access_bus()->run();
}

void media::ServiceSkeleton::stop()
{
    access_bus()->stop();
}
