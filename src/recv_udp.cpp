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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include "common_features.h"
#if SPEAD2_USE_RECVMMSG
# include <sys/socket.h>
# include <sys/types.h>
#endif
#include <cstdint>
#include <cstring>
#include <functional>
#include <boost/asio.hpp>
#include <iostream>
#include <mutex>
#include "recv_reader.h"
#include "recv_udp.h"
#include "common_logging.h"
#include <iostream>

namespace spead2
{
namespace recv
{

constexpr std::size_t udp_reader::default_max_size;
constexpr std::size_t udp_reader::default_buffer_size;

udp_reader::udp_reader(
    stream &owner,
    boost::asio::ip::udp::socket &&socket,
    const boost::asio::ip::udp::endpoint &endpoint,
    std::size_t max_size,
    std::size_t buffer_size)
    : reader(owner), socket(std::move(socket)), max_size(max_size),
#if SPEAD2_USE_RECVMMSG
    buffers(mmsg_count), msgvec(mmsg_count)
#else
    buffer(new std::uint8_t[max_size + 1])
#endif
{
    assert(&this->socket.get_io_service() == &get_io_service());
#if SPEAD2_USE_RECVMMSG
    for (std::size_t i = 0; i < mmsg_count; i++)
    {
        // Allocate one extra byte so that overflow can be detected
        buffers[i].data.reset(new std::uint8_t[max_size + 1]);
        buffers[i].iov.iov_base = (void *) buffers[i].data.get();
        buffers[i].iov.iov_len = max_size + 1;
        std::memset(&msgvec[i], 0, sizeof(msgvec[i]));
        msgvec[i].msg_hdr.msg_iov = &buffers[i].iov;
        msgvec[i].msg_hdr.msg_iovlen = 1;
    }
#endif

    if (buffer_size != 0)
    {
        boost::asio::socket_base::receive_buffer_size option(buffer_size);
        boost::system::error_code ec;
        this->socket.set_option(option, ec);
        if (ec)
        {
            log_warning("request for buffer size %s failed (%s): refer to documentation for details on increasing buffer size",
                        buffer_size, ec.message());
        }
        else
        {
            // Linux silently clips to the maximum allowed size
            boost::asio::socket_base::receive_buffer_size actual;
            this->socket.get_option(actual);
            if (std::size_t(actual.value()) < buffer_size)
            {
                log_warning("requested buffer size %d but only received %d: refer to documentation for details on increasing buffer size",
                            buffer_size, actual.value());
            }
        }
    }
    this->socket.bind(endpoint);
    enqueue_receive();
}

static boost::asio::ip::udp::socket make_multicast_v4_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::udp::endpoint &endpoint,
    const boost::asio::ip::address &interface_address)
{
    if (!endpoint.address().is_v4() || !endpoint.address().is_multicast())
        throw std::invalid_argument("endpoint is not an IPv4 multicast address");
    if (!interface_address.is_v4())
        throw std::invalid_argument("interface address is not an IPv4 address");
    boost::asio::ip::udp::socket socket(io_service, endpoint.protocol());
    socket.set_option(boost::asio::socket_base::reuse_address(true));
    socket.set_option(boost::asio::ip::multicast::join_group(
        endpoint.address().to_v4(), interface_address.to_v4()));
    return socket;
}

static boost::asio::ip::udp::socket make_multicast_v6_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::udp::endpoint &endpoint,
    unsigned int interface_index)
{
    if (!endpoint.address().is_v6() || !endpoint.address().is_multicast())
        throw std::invalid_argument("endpoint is not an IPv6 multicast address");
    boost::asio::ip::udp::socket socket(io_service, endpoint.protocol());
    socket.set_option(boost::asio::socket_base::reuse_address(true));
    socket.set_option(boost::asio::ip::multicast::join_group(
        endpoint.address().to_v6(), interface_index));
    return socket;
}

static boost::asio::ip::udp::socket make_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::udp::endpoint &endpoint)
{
    boost::asio::ip::udp::socket socket(io_service, endpoint.protocol());
    if (endpoint.address().is_multicast())
    {
        socket.set_option(boost::asio::socket_base::reuse_address(true));
        socket.set_option(boost::asio::ip::multicast::join_group(endpoint.address()));
    }
    return socket;
}

udp_reader::udp_reader(
    stream &owner,
    const boost::asio::ip::udp::endpoint &endpoint,
    std::size_t max_size,
    std::size_t buffer_size)
    : udp_reader(
        owner,
        make_socket(owner.get_io_service(), endpoint),
        endpoint, max_size, buffer_size)
{
}

udp_reader::udp_reader(
    stream &owner,
    const boost::asio::ip::udp::endpoint &endpoint,
    std::size_t max_size,
    std::size_t buffer_size,
    const boost::asio::ip::address &interface_address)
    : udp_reader(
        owner,
        make_multicast_v4_socket(owner.get_io_service(), endpoint, interface_address),
        endpoint, max_size, buffer_size)
{
}

udp_reader::udp_reader(
    stream &owner,
    const boost::asio::ip::udp::endpoint &endpoint,
    std::size_t max_size,
    std::size_t buffer_size,
    unsigned int interface_index)
    : udp_reader(
        owner,
        make_multicast_v6_socket(owner.get_io_service(), endpoint, interface_index),
        endpoint, max_size, buffer_size)
{
}

void udp_reader::process_one_packet(const std::uint8_t *data, std::size_t length)
{
    if (length <= max_size && length > 0)
    {
        // If it's bigger, the packet might have been truncated
        packet_header packet;
        std::size_t size = decode_packet(packet, data, length);
        if (size == length)
        {
            get_stream_base().add_packet(packet);
            if (get_stream_base().is_stopped())
            {
                log_debug("UDP reader: end of stream detected");
            }
        }
        else if (size != 0)
        {
            log_info("discarding packet due to size mismatch (%1% != %2%)",
                     size, length);
        }
    }
    else if (length > max_size)
        log_info("dropped packet due to truncation");
}

void udp_reader::process_packets()
{
#if SPEAD2_USE_RECVMMSG
    while (resume_first < resume_last)
    {
        if (get_stream_base().is_stopped())
        {
            log_info("UDP reader: discarding packet received after stream stopped");
            resume_first = resume_last = 0;
            break;
        }
        if (get_stream_base().is_paused())
        {
            pause();
            break;
        }
        process_one_packet(buffers[resume_first].data.get(), msgvec[resume_first].msg_len);
        resume_first++;
    }
#else
    if (get_stream_base().is_stopped())
        log_info("UDP reader: discarding packet received after stream stopped");
    else if (get_stream_base().is_paused())
        pause();
    else
        process_one_packet(buffer.get(), length);
#endif
}

void udp_reader::packet_handler(
    const boost::system::error_code &error,
    std::size_t bytes_transferred)
{
    std::lock_guard<std::mutex> lock(get_stream_mutex());
    if (!error)
    {
#if SPEAD2_USE_RECVMMSG
        int received = recvmmsg(socket.native_handle(), msgvec.data(), msgvec.size(),
                                MSG_DONTWAIT, nullptr);
        log_debug("recvmmsg returned %1%", received);
        if (received == -1)
        {
            std::error_code code(errno, std::system_category());
            log_warning("recvmmsg failed: %1% (%2%)", code.value(), code.message());
        }
        resume_first = 0;
        resume_last = std::max(0, received);
        process_packets();
#else
        length = bytes_transferred;
        process_packets();
#endif
    }
    // TODO: log the error if there was one
    enqueue_receive();
}

void udp_reader::enqueue_receive()
{
    using namespace std::placeholders;

    if (get_stream_base().is_stopped())
        stopped_promise.set_value();
    else if (!is_paused())
    {
        socket.async_receive_from(
#if SPEAD2_USE_RECVMMSG
            boost::asio::null_buffers(),
#else
            boost::asio::buffer(buffer.get(), max_size + 1),
#endif
            endpoint,
            std::bind(&udp_reader::packet_handler, this, _1, _2));
    }
}

void udp_reader::resume_handler()
{
    std::lock_guard<std::mutex> lock(get_stream_mutex());
    process_packets();
    enqueue_receive();
}

void udp_reader::stop()
{
    /* asio guarantees that closing a socket will cancel any pending
     * operations on it.
     * Don't put any logging here: it could be running in a shutdown
     * path where it is no longer safe to do so.
     */
    socket.close();
}

void udp_reader::join()
{
    stopped_promise.get_future().get();
}

} // namespace recv
} // namespace spead2
