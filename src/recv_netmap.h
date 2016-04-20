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
 * Support for netmap.
 */

#ifndef SPEAD2_RECV_NETMAP_H
#define SPEAD2_RECV_NETMAP_H

#if SPEAD2_USE_NETMAP

#define NETMAP_WITH_LIBS
#include <cstdint>
#include <string>
#include <atomic>
#include <boost/asio.hpp>
#include <net/netmap_user.h>
#include "recv_bypass.h"

namespace spead2
{
namespace recv
{
namespace detail
{

class nm_desc_destructor
{
public:
    void operator()(nm_desc *) const;
};

class bypass_service_netmap : public bypass_service
{
private:
    std::unique_ptr<nm_desc, nm_desc_destructor> desc;
    std::atomic<bool> stop;
    std::future<void> run_future;

    void run();

public:
    bypass_service_netmap(const std::string &interface);
    virtual ~bypass_service_netmap() override;
};

} // namespace detail
} // namespace recv
} // namespace spead2

#endif // SPEAD2_USE_NETMAP

#endif // SPEAD2_RECV_NETMAP_H
