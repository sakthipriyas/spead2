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
 */

#if SPEAD2_USE_NETMAP

#include <cstdint>
#include <cerrno>
#include <stdexcept>
#include <future>
#include <boost/asio.hpp>
#include <system_error>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include "recv_reader.h"
#include "recv_netmap.h"
#include "common_logging.h"
#include "common_thread_pool.h"

namespace spead2
{
namespace recv
{
namespace detail
{

void nm_desc_destructor::operator()(nm_desc *d) const
{
    // We wrap the fd in an asio handle, which takes care of closing it.
    // To prevent nm_close from closing it too, we nullify it here.
    d->fd = -1;
    int status = nm_close(d);
    if (status != 0)
    {
        std::error_code code(status, std::system_category());
        log_warning("Failed to close the netmap fd: %1% (%2%)", code.value(), code.message());
    }
}

bypass_service_netmap::bypass_service_netmap(boost::asio::io_service &io_service, const std::string &interface)
    : bypass_service(io_service),
    desc(nm_open(("netmap:" + interface + "*").c_str(), NULL, 0, NULL)),
    handle(io_service)
{
    if (!desc)
        throw std::system_error(errno, std::system_category());
    handle.assign(desc->fd);
    enqueue_receive();
}

bypass_service_netmap::bypass_service_netmap(thread_pool &pool, const std::string &interface)
    : bypass_service_netmap(pool.get_io_service(), interface)
{
}

bypass_service_netmap::~bypass_service_netmap()
{
    handle.cancel();
    stopped_promise.get_future().get();
}

void bypass_service_netmap::enqueue_receive()
{
    handle.async_read_some(
        boost::asio::null_buffers(),
        strand.wrap([this] (const boost::system::error_code &error, std::size_t bytes_transferred)
        {
            receive(error);
        }));
}

void bypass_service_netmap::receive(const boost::system::error_code &error)
{
    if (error)
    {
        if (error == boost::asio::error::operation_aborted)
        {
            stopped_promise.set_value();
            return;
        }
        else
            log_warning("Error waiting for netmap socket: %1%", error.message());
    }
    else
    {
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
    enqueue_receive();
}

} // namespace detail
} // namespace recv
} // namespace spead2

#endif // SPEAD2_USE_NETMAP
