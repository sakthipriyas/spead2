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

#include <boost/asio.hpp>
#include "recv_multicast.h"

namespace spead2
{
namespace recv
{

boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address_v4 &address,
    const boost::asio::ip::address_v4 &interface_address)
{
    using boost::asio::ip::udp;
    if (!address.is_multicast())
        throw std::invalid_argument("address is not an IPv4 multicast address");
    udp::socket socket(io_service, udp::v4());
    socket.set_option(boost::asio::socket_base::reuse_address(true));
    socket.set_option(boost::asio::ip::multicast::join_group(
        address, interface_address));
    return socket;
}

boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address &address,
    const boost::asio::ip::address &interface_address)
{
    if (!address.is_v4())
        throw std::invalid_argument("address is not an IPv4 address");
    if (!interface_address.is_v4())
        throw std::invalid_argument("interface address it not an IPv4 address");
    return make_multicast_socket(io_service, address.to_v4(), interface_address.to_v4());
}

boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address_v6 &address,
    unsigned int interface_index)
{
    using boost::asio::ip::udp;
    if (!address.is_multicast())
        throw std::invalid_argument("address is not an IPv6 multicast address");
    udp::socket socket(io_service, udp::v6());
    socket.set_option(boost::asio::socket_base::reuse_address(true));
    socket.set_option(boost::asio::ip::multicast::join_group(
        address, interface_index));
    return socket;
}

boost::asio::ip::udp::socket make_multicast_socket(
    boost::asio::io_service &io_service,
    const boost::asio::ip::address &address,
    unsigned int interface_index)
{
    if (!address.is_v6())
        throw std::invalid_argument("address is not an IPv6 address");
    return make_multicast_socket(io_service, address.to_v6(), interface_index);
}

boost::asio::ip::udp::socket make_socket(
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

} // namespace recv
} // namespace spead2
