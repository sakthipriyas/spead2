/* Copyright 2015 SKA South Africa
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 */

#ifndef SPEAD2_RECV_UDP_H
#define SPEAD2_RECV_UDP_H

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include "common_features.h"
#if SPEAD2_USE_RECVMMSG
# include <sys/socket.h>
# include <sys/types.h>
#endif
#include <cstdint>
#include <boost/asio.hpp>
#include "recv_reader.h"
#include "recv_stream.h"

namespace spead2
{
namespace recv
{

/**
 * Asynchronous stream reader that receives packets over UDP.
 *
 * @todo Log errors somehow?
 */
class udp_reader : public reader
{
private:
    /// UDP socket we are listening on
    boost::asio::ip::udp::socket socket;
    /// Unused, but need to provide the memory for asio to write to
    boost::asio::ip::udp::endpoint endpoint;
    /// Maximum packet size we will accept
    std::size_t max_size;
    struct packet_buffer
    {
        std::unique_ptr<std::uint8_t[]> data;
#if SPEAD2_USE_RECVMMSG
        iovec iov;
#else
        // With recvmmsg, the length is stored in msgvec
        std::size_t length;
#endif
    };
#if SPEAD2_USE_RECVMMSG
    /// Buffer for asynchronous receive, of size @a max_size + 1.
    std::vector<packet_buffer> buffers;
    /// recvmmsg control structures
    std::vector<mmsghdr> msgvec;
#else
    std::array<packet_buffer, 1> buffers;
#endif
    /// First packet to reprocess when resuming from pause
    std::size_t resume_first = 0;
    /// Total number of packets received (used when resuming from pause)
    std::size_t resume_last = 0;
    /// Internal state machine state
    state_t state = state_t::PAUSED;
    /// Promise filled on transition to STOPPED
    std::promise<void> stopped_promise;

    /// Start an asynchronous receive
    void enqueue_receive();

    /**
     * Handle a single received packet.
     *
     * @pre The stream is neither stopped nor paused.
     */
    void process_one_packet(const std::uint8_t *data, std::size_t length);

    /**
     * Handle multiple packets, stored in the class, until the stream is
     * stopped or paused.
     */
    void process_packets();

    /// Callback on completion of asynchronous receive
    void packet_handler(
        const boost::system::error_code &error,
        std::size_t bytes_transferred);

    void update_state(bool have_callback);

public:
    /// Maximum packet size, if none is explicitly passed to the constructor
    static constexpr std::size_t default_max_size = 9200;
    /// Socket receive buffer size, if none is explicitly passed to the constructor
    static constexpr std::size_t default_buffer_size = 8 * 1024 * 1024;
    /// Number of packets to receive in one go, if recvmmsg support is present
    static constexpr std::size_t mmsg_count = 64;

    /**
     * Constructor.
     *
     * If @a endpoint is a multicast address, then this constructor will
     * subscribe to the multicast group, and also set @c SO_REUSEADDR so that
     * multiple sockets can be subscribed to the multicast group.
     *
     * @param owner        Owning stream
     * @param endpoint     Address on which to listen
     * @param max_size     Maximum packet size that will be accepted.
     * @param buffer_size  Requested socket buffer size. Note that the
     *                     operating system might not allow a buffer size
     *                     as big as the default.
     */
    udp_reader(
        stream &owner,
        const boost::asio::ip::udp::endpoint &endpoint,
        std::size_t max_size = default_max_size,
        std::size_t buffer_size = default_buffer_size);

    /**
     * Constructor with explicit multicast interface address (IPv4 only).
     *
     * The socket will have @c SO_REUSEADDR set, so that multiple sockets can
     * all listen to the same multicast stream. If you want to let the
     * system pick the interface for the multicast subscription, use
     * @c boost::asio::ip::address_v4::any(), or use the default constructor.
     *
     * @param owner        Owning stream
     * @param endpoint     Multicast group and port
     * @param max_size     Maximum packet size that will be accepted.
     * @param buffer_size  Requested socket buffer size.
     * @param interface_address  Address of the interface which should join the group
     *
     * @throws std::invalid_argument If @a endpoint is not an IPv4 multicast address
     * @throws std::invalid_argument If @a interface_address is not an IPv4 address
     */
    udp_reader(
        stream &owner,
        const boost::asio::ip::udp::endpoint &endpoint,
        std::size_t max_size,
        std::size_t buffer_size,
        const boost::asio::ip::address &interface_address);

    /**
     * Constructor with explicit multicast interface index (IPv6 only).
     *
     * @param owner        Owning stream
     * @param endpoint     Multicast group and port
     * @param max_size     Maximum packet size that will be accepted.
     * @param buffer_size  Requested socket buffer size.
     * @param interface_index  Address of the interface which should join the group
     * @throws std::invalid_argument If @a endpoint is not an IPv6 multicast address
     *
     * @see @ref make_multicast_socket, if_nametoindex(3)
     */
    udp_reader(
        stream &owner,
        const boost::asio::ip::udp::endpoint &endpoint,
        std::size_t max_size,
        std::size_t buffer_size,
        unsigned int interface_index);

    /**
     * Constructor using an existing socket. This allows socket options (e.g.,
     * multicast subscriptions) to be fine-tuned by the caller. The socket
     * should not be bound. Note that there is no special handling for
     * multicast addresses here.
     *
     * @param owner        Owning stream
     * @param socket       Existing socket which will be taken over. It must
     *                     use the same I/O service as @a owner.
     * @param endpoint     Address on which to listen
     * @param max_size     Maximum packet size that will be accepted.
     * @param buffer_size  Requested socket buffer size. Note that the
     *                     operating system might not allow a buffer size
     *                     as big as the default.
     */
    udp_reader(
        stream &owner,
        boost::asio::ip::udp::socket &&socket,
        const boost::asio::ip::udp::endpoint &endpoint,
        std::size_t max_size = default_max_size,
        std::size_t buffer_size = default_buffer_size);

    virtual void state_change() override;
    virtual void join() override;
};

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_UDP_H
