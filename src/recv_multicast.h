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
 *
 * Utilities for subscribing to multicast groups.
 */

#ifndef SPEAD2_RECV_MULTICAST_H
#define SPEAD2_RECV_MULTICAST_H

#include <boost/asio.hpp>

namespace spead2
{
namespace recv
{

/**
 * Create an unbound UDP4 socket and subscribe it to a multicast group.
 *
 * The socket will have @c SO_REUSEADDR set, so that multiple sockets can
 * all listen to the same multicast stream. If you want to let the
 * system pick the interface for the multicast subscription, use
 * @c boost::asio::ip::address_v4::any().
 *
 * @param io_service         io_service passed to the socket constructor
 * @param address            Multicast group
 * @param interface_address  Address of the interface which should join the group
 * @throws std::invalid_argument If @a endpoint is not a multicast address
 */
boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address_v4 &address,
    const boost::asio::ip::address_v4 &interface_address);

/**
 * Create an unbound UDP4 socket and subscribe it to a multicast group.
 *
 * The socket will have @c SO_REUSEADDR set, so that multiple sockets can
 * all listen to the same multicast stream. If you want to let the
 * system pick the interface for the multicast subscription, use
 * @c boost::asio::ip::address_v4::any().
 *
 * @param io_service         io_service passed to the socket constructor
 * @param address            Multicast group
 * @param interface_address  Address of the interface which should join the group
 * @throws std::invalid_argument If @a endpoint is not a multicast address
 * @throws std::invalid_argument If @a endpoint or @a interface_address is not an IPv4 address
 */
boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address &address,
    const boost::asio::ip::address &interface_address);

/**
 * Create an unbound UDP6 socket and subscribe it to a multicast group.
 *
 * The socket will have @c SO_REUSEADDR set, so that multiple sockets can
 * all listen to the same multicast stream. If you want to let the
 * system pick the interface for the multicast subscription, set
 * @a interface_index to 0, or use the standard constructor.
 *
 * @param io_service       io_service passed to the socket constructor
 * @param address          Multicast group
 * @param interface_index  Address of the interface which should join the group
 * @throws std::invalid_argument If @a endpoint is not a multicast address
 *
 * @see if_nametoindex(3)
 */
boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address_v6 &address,
    unsigned int interface_index);

/**
 * Create an unbound UDP6 socket and subscribe it to a multicast group.
 *
 * The socket will have @c SO_REUSEADDR set, so that multiple sockets can
 * all listen to the same multicast stream. If you want to let the
 * system pick the interface for the multicast subscription, set
 * @a interface_index to 0, or use the standard constructor.
 *
 * @param io_service       io_service passed to the socket constructor
 * @param address          Multicast group
 * @param interface_index  Address of the interface which should join the group
 * @throws std::invalid_argument If @a endpoint is not a multicast address
 * @throws std::invalid_argument If @a endpoint is not an IPv6 address
 *
 * @see if_nametoindex(3)
 */
boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address &address,
    unsigned int interface_index);

/**
 * Create an unbound UDP socket, and subscribe it to a multicast group if the
 * endpoint address is a multicast address.
 */
boost::asio::ip::udp::socket make_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::udp::endpoint &endpoint);

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_MULTICAST_H
