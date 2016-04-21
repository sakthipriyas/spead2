/* Copyright 2015, 2016 SKA South Africa
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
#include <thread>
#include <memory>
#include <boost/asio.hpp>
#include <net/netmap_user.h>
#include "recv_bypass.h"
#include "common_semaphore.h"

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

class bypass_service_netmap : public bypass_service, public std::enable_shared_from_this<bypass_service_netmap>
{
private:
    /**
     * netmap handle. Only the internal thread may access or modify it.
     */
    std::unique_ptr<nm_desc, nm_desc_destructor> desc;
    /**
     * Semaphore that is put when @ref stop() is called.
     */
    semaphore_fd wake;

    /**
     * Reference to self, owned by the thread. When the thread is live, it is
     * owned by the thread.
     */
    std::shared_ptr<bypass_service_netmap> self;
    std::thread run_thread;
    void run();  ///< Thread function

    virtual void start() override;
    virtual void stop() override;

public:
    bypass_service_netmap(const std::string &type, const std::string &interface);
};

} // namespace detail
} // namespace recv
} // namespace spead2

#endif // SPEAD2_USE_NETMAP

#endif // SPEAD2_RECV_NETMAP_H
