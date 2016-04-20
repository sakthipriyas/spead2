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

#if SPEAD2_USE_NETMAP

#include <cstdint>
#include <cerrno>
#include <stdexcept>
#include <boost/asio.hpp>
#include <system_error>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include "recv_reader.h"
#include "recv_netmap.h"
#include "common_logging.h"

namespace spead2
{
namespace recv
{
namespace detail
{

void nm_desc_destructor::operator()(nm_desc *d) const
{
    int status = nm_close(d);
    if (status != 0)
    {
        std::error_code code(status, std::system_category());
        log_warning("Failed to close the netmap fd: %1% (%2%)", code.value(), code.message());
    }
}

bypass_service_netmap::bypass_service_netmap(const std::string &interface)
    : desc(nm_open(("netmap:" + interface + "*").c_str(), NULL, 0, NULL)),
    stop(false)
{
    if (!desc)
        throw std::system_error(errno, std::system_category());
    run_future = std::async(std::launch::async, [this] { run(); });
}

bypass_service_netmap::~bypass_service_netmap()
{
    stop.store(true);
    try
    {
        run_future.get();
    }
    catch (std::exception &e)
    {
        log_warning("Exception in netmap thread: %1%", e.what());
    }
}

void bypass_service_netmap::run()
{
    struct pollfd fds[1] = {};
    fds[0].fd = desc->fd;
    fds[0].events = POLLIN;
    // TODO: use a second fd instead of a stopped flag
    while (!stop.load())
    {
        int status = poll(fds, 1, 10);
        if (status < 0)
        {
            std::error_code code(status, std::system_category());
            log_warning("poll failed: %1% (%2%)", code.value(), code.message());
            continue;
        }
        else if (status == 0)
            continue; // timeout, check the stopped flag again

        std::lock_guard<std::mutex> lock(mutex);
        for (int ri = desc->first_rx_ring; ri <= desc->last_rx_ring; ri++)
        {
            netmap_ring *ring = NETMAP_RXRING(desc->nifp, ri);
            ring->flags |= NR_FORWARD | NR_TIMESTAMP;
            for (unsigned int i = ring->cur; i != ring->tail; i = nm_ring_next(ring, i))
            {
                auto &slot = ring->slot[i];
                const unsigned char *data = (const unsigned char *) NETMAP_BUF(ring, slot.buf_idx);
                // Skip even trying to process packets in the host ring
                bool used = false;
                if (ri != desc->req.nr_rx_rings
                    && !(slot.flags & NS_MOREFRAG))
                {
                    used = process_packet(data, slot.len);
                }
                if (!used)
                    slot.flags |= NS_FORWARD;
            }
            ring->cur = ring->head = ring->tail;
        }
    }
}

} // namespace detail
} // namespace recv
} // namespace spead2

#endif // SPEAD2_USE_NETMAP
